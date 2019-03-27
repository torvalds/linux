//===-- AMDGPUSubtarget.cpp - AMDGPU Subtarget Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Implements the AMDGPU specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUSubtarget.h"
#include "AMDGPU.h"
#include "AMDGPUTargetMachine.h"
#include "AMDGPUCallLowering.h"
#include "AMDGPUInstructionSelector.h"
#include "AMDGPULegalizerInfo.h"
#include "AMDGPURegisterBankInfo.h"
#include "SIMachineFunctionInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include <algorithm>

using namespace llvm;

#define DEBUG_TYPE "amdgpu-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#define AMDGPUSubtarget GCNSubtarget
#include "AMDGPUGenSubtargetInfo.inc"
#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#undef AMDGPUSubtarget
#include "R600GenSubtargetInfo.inc"

GCNSubtarget::~GCNSubtarget() = default;

R600Subtarget &
R600Subtarget::initializeSubtargetDependencies(const Triple &TT,
                                               StringRef GPU, StringRef FS) {
  SmallString<256> FullFS("+promote-alloca,+dx10-clamp,");
  FullFS += FS;
  ParseSubtargetFeatures(GPU, FullFS);

  // FIXME: I don't think think Evergreen has any useful support for
  // denormals, but should be checked. Should we issue a warning somewhere
  // if someone tries to enable these?
  if (getGeneration() <= AMDGPUSubtarget::NORTHERN_ISLANDS) {
    FP32Denormals = false;
  }

  HasMulU24 = getGeneration() >= EVERGREEN;
  HasMulI24 = hasCaymanISA();

  return *this;
}

GCNSubtarget &
GCNSubtarget::initializeSubtargetDependencies(const Triple &TT,
                                                 StringRef GPU, StringRef FS) {
  // Determine default and user-specified characteristics
  // On SI+, we want FP64 denormals to be on by default. FP32 denormals can be
  // enabled, but some instructions do not respect them and they run at the
  // double precision rate, so don't enable by default.
  //
  // We want to be able to turn these off, but making this a subtarget feature
  // for SI has the unhelpful behavior that it unsets everything else if you
  // disable it.
  //
  // Similarly we want enable-prt-strict-null to be on by default and not to
  // unset everything else if it is disabled

  SmallString<256> FullFS("+promote-alloca,+dx10-clamp,+load-store-opt,");

  if (isAmdHsaOS()) // Turn on FlatForGlobal for HSA.
    FullFS += "+flat-address-space,+flat-for-global,+unaligned-buffer-access,+trap-handler,";

  // FIXME: I don't think think Evergreen has any useful support for
  // denormals, but should be checked. Should we issue a warning somewhere
  // if someone tries to enable these?
  if (getGeneration() >= AMDGPUSubtarget::SOUTHERN_ISLANDS) {
    FullFS += "+fp64-fp16-denormals,";
  } else {
    FullFS += "-fp32-denormals,";
  }

  FullFS += "+enable-prt-strict-null,"; // This is overridden by a disable in FS

  FullFS += FS;

  ParseSubtargetFeatures(GPU, FullFS);

  // We don't support FP64 for EG/NI atm.
  assert(!hasFP64() || (getGeneration() >= AMDGPUSubtarget::SOUTHERN_ISLANDS));

  // Unless +-flat-for-global is specified, turn on FlatForGlobal for all OS-es
  // on VI and newer hardware to avoid assertion failures due to missing ADDR64
  // variants of MUBUF instructions.
  if (!hasAddr64() && !FS.contains("flat-for-global")) {
    FlatForGlobal = true;
  }

  // Set defaults if needed.
  if (MaxPrivateElementSize == 0)
    MaxPrivateElementSize = 4;

  if (LDSBankCount == 0)
    LDSBankCount = 32;

  if (TT.getArch() == Triple::amdgcn) {
    if (LocalMemorySize == 0)
      LocalMemorySize = 32768;

    // Do something sensible for unspecified target.
    if (!HasMovrel && !HasVGPRIndexMode)
      HasMovrel = true;
  }

  HasFminFmaxLegacy = getGeneration() < AMDGPUSubtarget::VOLCANIC_ISLANDS;

  return *this;
}

AMDGPUSubtarget::AMDGPUSubtarget(const Triple &TT) :
  TargetTriple(TT),
  Has16BitInsts(false),
  HasMadMixInsts(false),
  FP32Denormals(false),
  FPExceptions(false),
  HasSDWA(false),
  HasVOP3PInsts(false),
  HasMulI24(true),
  HasMulU24(true),
  HasInv2PiInlineImm(false),
  HasFminFmaxLegacy(true),
  EnablePromoteAlloca(false),
  HasTrigReducedRange(false),
  LocalMemorySize(0),
  WavefrontSize(0)
  { }

GCNSubtarget::GCNSubtarget(const Triple &TT, StringRef GPU, StringRef FS,
                           const GCNTargetMachine &TM) :
    AMDGPUGenSubtargetInfo(TT, GPU, FS),
    AMDGPUSubtarget(TT),
    TargetTriple(TT),
    Gen(SOUTHERN_ISLANDS),
    IsaVersion(ISAVersion0_0_0),
    InstrItins(getInstrItineraryForCPU(GPU)),
    LDSBankCount(0),
    MaxPrivateElementSize(0),

    FastFMAF32(false),
    HalfRate64Ops(false),

    FP64FP16Denormals(false),
    DX10Clamp(false),
    FlatForGlobal(false),
    AutoWaitcntBeforeBarrier(false),
    CodeObjectV3(false),
    UnalignedScratchAccess(false),
    UnalignedBufferAccess(false),

    HasApertureRegs(false),
    EnableXNACK(false),
    TrapHandler(false),
    DebuggerInsertNops(false),
    DebuggerEmitPrologue(false),

    EnableHugePrivateBuffer(false),
    EnableLoadStoreOpt(false),
    EnableUnsafeDSOffsetFolding(false),
    EnableSIScheduler(false),
    EnableDS128(false),
    EnablePRTStrictNull(false),
    DumpCode(false),

    FP64(false),
    GCN3Encoding(false),
    CIInsts(false),
    VIInsts(false),
    GFX9Insts(false),
    SGPRInitBug(false),
    HasSMemRealTime(false),
    HasIntClamp(false),
    HasFmaMixInsts(false),
    HasMovrel(false),
    HasVGPRIndexMode(false),
    HasScalarStores(false),
    HasScalarAtomics(false),
    HasSDWAOmod(false),
    HasSDWAScalar(false),
    HasSDWASdst(false),
    HasSDWAMac(false),
    HasSDWAOutModsVOPC(false),
    HasDPP(false),
    HasR128A16(false),
    HasDLInsts(false),
    HasDotInsts(false),
    EnableSRAMECC(false),
    FlatAddressSpace(false),
    FlatInstOffsets(false),
    FlatGlobalInsts(false),
    FlatScratchInsts(false),
    AddNoCarryInsts(false),
    HasUnpackedD16VMem(false),

    ScalarizeGlobal(false),

    FeatureDisable(false),
    InstrInfo(initializeSubtargetDependencies(TT, GPU, FS)),
    TLInfo(TM, *this),
    FrameLowering(TargetFrameLowering::StackGrowsUp, getStackAlignment(), 0) {
  CallLoweringInfo.reset(new AMDGPUCallLowering(*getTargetLowering()));
  Legalizer.reset(new AMDGPULegalizerInfo(*this, TM));
  RegBankInfo.reset(new AMDGPURegisterBankInfo(*getRegisterInfo()));
  InstSelector.reset(new AMDGPUInstructionSelector(
  *this, *static_cast<AMDGPURegisterBankInfo *>(RegBankInfo.get()), TM));
}

unsigned AMDGPUSubtarget::getMaxLocalMemSizeWithWaveCount(unsigned NWaves,
  const Function &F) const {
  if (NWaves == 1)
    return getLocalMemorySize();
  unsigned WorkGroupSize = getFlatWorkGroupSizes(F).second;
  unsigned WorkGroupsPerCu = getMaxWorkGroupsPerCU(WorkGroupSize);
  unsigned MaxWaves = getMaxWavesPerEU();
  return getLocalMemorySize() * MaxWaves / WorkGroupsPerCu / NWaves;
}

unsigned AMDGPUSubtarget::getOccupancyWithLocalMemSize(uint32_t Bytes,
  const Function &F) const {
  unsigned WorkGroupSize = getFlatWorkGroupSizes(F).second;
  unsigned WorkGroupsPerCu = getMaxWorkGroupsPerCU(WorkGroupSize);
  unsigned MaxWaves = getMaxWavesPerEU();
  unsigned Limit = getLocalMemorySize() * MaxWaves / WorkGroupsPerCu;
  unsigned NumWaves = Limit / (Bytes ? Bytes : 1u);
  NumWaves = std::min(NumWaves, MaxWaves);
  NumWaves = std::max(NumWaves, 1u);
  return NumWaves;
}

unsigned
AMDGPUSubtarget::getOccupancyWithLocalMemSize(const MachineFunction &MF) const {
  const auto *MFI = MF.getInfo<SIMachineFunctionInfo>();
  return getOccupancyWithLocalMemSize(MFI->getLDSSize(), MF.getFunction());
}

std::pair<unsigned, unsigned>
AMDGPUSubtarget::getDefaultFlatWorkGroupSize(CallingConv::ID CC) const {
  switch (CC) {
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
    return std::make_pair(getWavefrontSize() * 2, getWavefrontSize() * 4);
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_LS:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_ES:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
    return std::make_pair(1, getWavefrontSize());
  default:
    return std::make_pair(1, 16 * getWavefrontSize());
  }
}

std::pair<unsigned, unsigned> AMDGPUSubtarget::getFlatWorkGroupSizes(
  const Function &F) const {
  // FIXME: 1024 if function.
  // Default minimum/maximum flat work group sizes.
  std::pair<unsigned, unsigned> Default =
    getDefaultFlatWorkGroupSize(F.getCallingConv());

  // TODO: Do not process "amdgpu-max-work-group-size" attribute once mesa
  // starts using "amdgpu-flat-work-group-size" attribute.
  Default.second = AMDGPU::getIntegerAttribute(
    F, "amdgpu-max-work-group-size", Default.second);
  Default.first = std::min(Default.first, Default.second);

  // Requested minimum/maximum flat work group sizes.
  std::pair<unsigned, unsigned> Requested = AMDGPU::getIntegerPairAttribute(
    F, "amdgpu-flat-work-group-size", Default);

  // Make sure requested minimum is less than requested maximum.
  if (Requested.first > Requested.second)
    return Default;

  // Make sure requested values do not violate subtarget's specifications.
  if (Requested.first < getMinFlatWorkGroupSize())
    return Default;
  if (Requested.second > getMaxFlatWorkGroupSize())
    return Default;

  return Requested;
}

std::pair<unsigned, unsigned> AMDGPUSubtarget::getWavesPerEU(
  const Function &F) const {
  // Default minimum/maximum number of waves per execution unit.
  std::pair<unsigned, unsigned> Default(1, getMaxWavesPerEU());

  // Default/requested minimum/maximum flat work group sizes.
  std::pair<unsigned, unsigned> FlatWorkGroupSizes = getFlatWorkGroupSizes(F);

  // If minimum/maximum flat work group sizes were explicitly requested using
  // "amdgpu-flat-work-group-size" attribute, then set default minimum/maximum
  // number of waves per execution unit to values implied by requested
  // minimum/maximum flat work group sizes.
  unsigned MinImpliedByFlatWorkGroupSize =
    getMaxWavesPerEU(FlatWorkGroupSizes.second);
  bool RequestedFlatWorkGroupSize = false;

  // TODO: Do not process "amdgpu-max-work-group-size" attribute once mesa
  // starts using "amdgpu-flat-work-group-size" attribute.
  if (F.hasFnAttribute("amdgpu-max-work-group-size") ||
      F.hasFnAttribute("amdgpu-flat-work-group-size")) {
    Default.first = MinImpliedByFlatWorkGroupSize;
    RequestedFlatWorkGroupSize = true;
  }

  // Requested minimum/maximum number of waves per execution unit.
  std::pair<unsigned, unsigned> Requested = AMDGPU::getIntegerPairAttribute(
    F, "amdgpu-waves-per-eu", Default, true);

  // Make sure requested minimum is less than requested maximum.
  if (Requested.second && Requested.first > Requested.second)
    return Default;

  // Make sure requested values do not violate subtarget's specifications.
  if (Requested.first < getMinWavesPerEU() ||
      Requested.first > getMaxWavesPerEU())
    return Default;
  if (Requested.second > getMaxWavesPerEU())
    return Default;

  // Make sure requested values are compatible with values implied by requested
  // minimum/maximum flat work group sizes.
  if (RequestedFlatWorkGroupSize &&
      Requested.first < MinImpliedByFlatWorkGroupSize)
    return Default;

  return Requested;
}

bool AMDGPUSubtarget::makeLIDRangeMetadata(Instruction *I) const {
  Function *Kernel = I->getParent()->getParent();
  unsigned MinSize = 0;
  unsigned MaxSize = getFlatWorkGroupSizes(*Kernel).second;
  bool IdQuery = false;

  // If reqd_work_group_size is present it narrows value down.
  if (auto *CI = dyn_cast<CallInst>(I)) {
    const Function *F = CI->getCalledFunction();
    if (F) {
      unsigned Dim = UINT_MAX;
      switch (F->getIntrinsicID()) {
      case Intrinsic::amdgcn_workitem_id_x:
      case Intrinsic::r600_read_tidig_x:
        IdQuery = true;
        LLVM_FALLTHROUGH;
      case Intrinsic::r600_read_local_size_x:
        Dim = 0;
        break;
      case Intrinsic::amdgcn_workitem_id_y:
      case Intrinsic::r600_read_tidig_y:
        IdQuery = true;
        LLVM_FALLTHROUGH;
      case Intrinsic::r600_read_local_size_y:
        Dim = 1;
        break;
      case Intrinsic::amdgcn_workitem_id_z:
      case Intrinsic::r600_read_tidig_z:
        IdQuery = true;
        LLVM_FALLTHROUGH;
      case Intrinsic::r600_read_local_size_z:
        Dim = 2;
        break;
      default:
        break;
      }
      if (Dim <= 3) {
        if (auto Node = Kernel->getMetadata("reqd_work_group_size"))
          if (Node->getNumOperands() == 3)
            MinSize = MaxSize = mdconst::extract<ConstantInt>(
                                  Node->getOperand(Dim))->getZExtValue();
      }
    }
  }

  if (!MaxSize)
    return false;

  // Range metadata is [Lo, Hi). For ID query we need to pass max size
  // as Hi. For size query we need to pass Hi + 1.
  if (IdQuery)
    MinSize = 0;
  else
    ++MaxSize;

  MDBuilder MDB(I->getContext());
  MDNode *MaxWorkGroupSizeRange = MDB.createRange(APInt(32, MinSize),
                                                  APInt(32, MaxSize));
  I->setMetadata(LLVMContext::MD_range, MaxWorkGroupSizeRange);
  return true;
}

uint64_t AMDGPUSubtarget::getExplicitKernArgSize(const Function &F,
                                                 unsigned &MaxAlign) const {
  assert(F.getCallingConv() == CallingConv::AMDGPU_KERNEL ||
         F.getCallingConv() == CallingConv::SPIR_KERNEL);

  const DataLayout &DL = F.getParent()->getDataLayout();
  uint64_t ExplicitArgBytes = 0;
  MaxAlign = 1;

  for (const Argument &Arg : F.args()) {
    Type *ArgTy = Arg.getType();

    unsigned Align = DL.getABITypeAlignment(ArgTy);
    uint64_t AllocSize = DL.getTypeAllocSize(ArgTy);
    ExplicitArgBytes = alignTo(ExplicitArgBytes, Align) + AllocSize;
    MaxAlign = std::max(MaxAlign, Align);
  }

  return ExplicitArgBytes;
}

unsigned AMDGPUSubtarget::getKernArgSegmentSize(const Function &F,
                                                unsigned &MaxAlign) const {
  uint64_t ExplicitArgBytes = getExplicitKernArgSize(F, MaxAlign);

  unsigned ExplicitOffset = getExplicitKernelArgOffset(F);

  uint64_t TotalSize = ExplicitOffset + ExplicitArgBytes;
  unsigned ImplicitBytes = getImplicitArgNumBytes(F);
  if (ImplicitBytes != 0) {
    unsigned Alignment = getAlignmentForImplicitArgPtr();
    TotalSize = alignTo(ExplicitArgBytes, Alignment) + ImplicitBytes;
  }

  // Being able to dereference past the end is useful for emitting scalar loads.
  return alignTo(TotalSize, 4);
}

R600Subtarget::R600Subtarget(const Triple &TT, StringRef GPU, StringRef FS,
                             const TargetMachine &TM) :
  R600GenSubtargetInfo(TT, GPU, FS),
  AMDGPUSubtarget(TT),
  InstrInfo(*this),
  FrameLowering(TargetFrameLowering::StackGrowsUp, getStackAlignment(), 0),
  FMA(false),
  CaymanISA(false),
  CFALUBug(false),
  DX10Clamp(false),
  HasVertexCache(false),
  R600ALUInst(false),
  FP64(false),
  TexVTXClauseSize(0),
  Gen(R600),
  TLInfo(TM, initializeSubtargetDependencies(TT, GPU, FS)),
  InstrItins(getInstrItineraryForCPU(GPU)) { }

void GCNSubtarget::overrideSchedPolicy(MachineSchedPolicy &Policy,
                                      unsigned NumRegionInstrs) const {
  // Track register pressure so the scheduler can try to decrease
  // pressure once register usage is above the threshold defined by
  // SIRegisterInfo::getRegPressureSetLimit()
  Policy.ShouldTrackPressure = true;

  // Enabling both top down and bottom up scheduling seems to give us less
  // register spills than just using one of these approaches on its own.
  Policy.OnlyTopDown = false;
  Policy.OnlyBottomUp = false;

  // Enabling ShouldTrackLaneMasks crashes the SI Machine Scheduler.
  if (!enableSIScheduler())
    Policy.ShouldTrackLaneMasks = true;
}

unsigned GCNSubtarget::getOccupancyWithNumSGPRs(unsigned SGPRs) const {
  if (getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS) {
    if (SGPRs <= 80)
      return 10;
    if (SGPRs <= 88)
      return 9;
    if (SGPRs <= 100)
      return 8;
    return 7;
  }
  if (SGPRs <= 48)
    return 10;
  if (SGPRs <= 56)
    return 9;
  if (SGPRs <= 64)
    return 8;
  if (SGPRs <= 72)
    return 7;
  if (SGPRs <= 80)
    return 6;
  return 5;
}

unsigned GCNSubtarget::getOccupancyWithNumVGPRs(unsigned VGPRs) const {
  if (VGPRs <= 24)
    return 10;
  if (VGPRs <= 28)
    return 9;
  if (VGPRs <= 32)
    return 8;
  if (VGPRs <= 36)
    return 7;
  if (VGPRs <= 40)
    return 6;
  if (VGPRs <= 48)
    return 5;
  if (VGPRs <= 64)
    return 4;
  if (VGPRs <= 84)
    return 3;
  if (VGPRs <= 128)
    return 2;
  return 1;
}

unsigned GCNSubtarget::getReservedNumSGPRs(const MachineFunction &MF) const {
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();
  if (MFI.hasFlatScratchInit()) {
    if (getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS)
      return 6; // FLAT_SCRATCH, XNACK, VCC (in that order).
    if (getGeneration() == AMDGPUSubtarget::SEA_ISLANDS)
      return 4; // FLAT_SCRATCH, VCC (in that order).
  }

  if (isXNACKEnabled())
    return 4; // XNACK, VCC (in that order).
  return 2; // VCC.
}

unsigned GCNSubtarget::getMaxNumSGPRs(const MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();

  // Compute maximum number of SGPRs function can use using default/requested
  // minimum number of waves per execution unit.
  std::pair<unsigned, unsigned> WavesPerEU = MFI.getWavesPerEU();
  unsigned MaxNumSGPRs = getMaxNumSGPRs(WavesPerEU.first, false);
  unsigned MaxAddressableNumSGPRs = getMaxNumSGPRs(WavesPerEU.first, true);

  // Check if maximum number of SGPRs was explicitly requested using
  // "amdgpu-num-sgpr" attribute.
  if (F.hasFnAttribute("amdgpu-num-sgpr")) {
    unsigned Requested = AMDGPU::getIntegerAttribute(
      F, "amdgpu-num-sgpr", MaxNumSGPRs);

    // Make sure requested value does not violate subtarget's specifications.
    if (Requested && (Requested <= getReservedNumSGPRs(MF)))
      Requested = 0;

    // If more SGPRs are required to support the input user/system SGPRs,
    // increase to accommodate them.
    //
    // FIXME: This really ends up using the requested number of SGPRs + number
    // of reserved special registers in total. Theoretically you could re-use
    // the last input registers for these special registers, but this would
    // require a lot of complexity to deal with the weird aliasing.
    unsigned InputNumSGPRs = MFI.getNumPreloadedSGPRs();
    if (Requested && Requested < InputNumSGPRs)
      Requested = InputNumSGPRs;

    // Make sure requested value is compatible with values implied by
    // default/requested minimum/maximum number of waves per execution unit.
    if (Requested && Requested > getMaxNumSGPRs(WavesPerEU.first, false))
      Requested = 0;
    if (WavesPerEU.second &&
        Requested && Requested < getMinNumSGPRs(WavesPerEU.second))
      Requested = 0;

    if (Requested)
      MaxNumSGPRs = Requested;
  }

  if (hasSGPRInitBug())
    MaxNumSGPRs = AMDGPU::IsaInfo::FIXED_NUM_SGPRS_FOR_INIT_BUG;

  return std::min(MaxNumSGPRs - getReservedNumSGPRs(MF),
                  MaxAddressableNumSGPRs);
}

unsigned GCNSubtarget::getMaxNumVGPRs(const MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();

  // Compute maximum number of VGPRs function can use using default/requested
  // minimum number of waves per execution unit.
  std::pair<unsigned, unsigned> WavesPerEU = MFI.getWavesPerEU();
  unsigned MaxNumVGPRs = getMaxNumVGPRs(WavesPerEU.first);

  // Check if maximum number of VGPRs was explicitly requested using
  // "amdgpu-num-vgpr" attribute.
  if (F.hasFnAttribute("amdgpu-num-vgpr")) {
    unsigned Requested = AMDGPU::getIntegerAttribute(
      F, "amdgpu-num-vgpr", MaxNumVGPRs);

    // Make sure requested value is compatible with values implied by
    // default/requested minimum/maximum number of waves per execution unit.
    if (Requested && Requested > getMaxNumVGPRs(WavesPerEU.first))
      Requested = 0;
    if (WavesPerEU.second &&
        Requested && Requested < getMinNumVGPRs(WavesPerEU.second))
      Requested = 0;

    if (Requested)
      MaxNumVGPRs = Requested;
  }

  return MaxNumVGPRs;
}

namespace {
struct MemOpClusterMutation : ScheduleDAGMutation {
  const SIInstrInfo *TII;

  MemOpClusterMutation(const SIInstrInfo *tii) : TII(tii) {}

  void apply(ScheduleDAGInstrs *DAGInstrs) override {
    ScheduleDAGMI *DAG = static_cast<ScheduleDAGMI*>(DAGInstrs);

    SUnit *SUa = nullptr;
    // Search for two consequent memory operations and link them
    // to prevent scheduler from moving them apart.
    // In DAG pre-process SUnits are in the original order of
    // the instructions before scheduling.
    for (SUnit &SU : DAG->SUnits) {
      MachineInstr &MI2 = *SU.getInstr();
      if (!MI2.mayLoad() && !MI2.mayStore()) {
        SUa = nullptr;
        continue;
      }
      if (!SUa) {
        SUa = &SU;
        continue;
      }

      MachineInstr &MI1 = *SUa->getInstr();
      if ((TII->isVMEM(MI1) && TII->isVMEM(MI2)) ||
          (TII->isFLAT(MI1) && TII->isFLAT(MI2)) ||
          (TII->isSMRD(MI1) && TII->isSMRD(MI2)) ||
          (TII->isDS(MI1)   && TII->isDS(MI2))) {
        SU.addPredBarrier(SUa);

        for (const SDep &SI : SU.Preds) {
          if (SI.getSUnit() != SUa)
            SUa->addPred(SDep(SI.getSUnit(), SDep::Artificial));
        }

        if (&SU != &DAG->ExitSU) {
          for (const SDep &SI : SUa->Succs) {
            if (SI.getSUnit() != &SU)
              SI.getSUnit()->addPred(SDep(&SU, SDep::Artificial));
          }
        }
      }

      SUa = &SU;
    }
  }
};
} // namespace

void GCNSubtarget::getPostRAMutations(
    std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  Mutations.push_back(llvm::make_unique<MemOpClusterMutation>(&InstrInfo));
}

const AMDGPUSubtarget &AMDGPUSubtarget::get(const MachineFunction &MF) {
  if (MF.getTarget().getTargetTriple().getArch() == Triple::amdgcn)
    return static_cast<const AMDGPUSubtarget&>(MF.getSubtarget<GCNSubtarget>());
  else
    return static_cast<const AMDGPUSubtarget&>(MF.getSubtarget<R600Subtarget>());
}

const AMDGPUSubtarget &AMDGPUSubtarget::get(const TargetMachine &TM, const Function &F) {
  if (TM.getTargetTriple().getArch() == Triple::amdgcn)
    return static_cast<const AMDGPUSubtarget&>(TM.getSubtarget<GCNSubtarget>(F));
  else
    return static_cast<const AMDGPUSubtarget&>(TM.getSubtarget<R600Subtarget>(F));
}
