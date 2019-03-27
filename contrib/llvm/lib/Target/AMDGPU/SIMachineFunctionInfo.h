//==- SIMachineFunctionInfo.h - SIMachineFunctionInfo interface --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_SIMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_AMDGPU_SIMACHINEFUNCTIONINFO_H

#include "AMDGPUArgumentUsageInfo.h"
#include "AMDGPUMachineFunction.h"
#include "SIInstrInfo.h"
#include "SIRegisterInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include <array>
#include <cassert>
#include <utility>
#include <vector>

namespace llvm {

class MachineFrameInfo;
class MachineFunction;
class TargetRegisterClass;

class AMDGPUImagePseudoSourceValue : public PseudoSourceValue {
public:
  // TODO: Is the img rsrc useful?
  explicit AMDGPUImagePseudoSourceValue(const TargetInstrInfo &TII) :
    PseudoSourceValue(PseudoSourceValue::TargetCustom, TII) {}

  bool isConstant(const MachineFrameInfo *) const override {
    // This should probably be true for most images, but we will start by being
    // conservative.
    return false;
  }

  bool isAliased(const MachineFrameInfo *) const override {
    return true;
  }

  bool mayAlias(const MachineFrameInfo *) const override {
    return true;
  }
};

class AMDGPUBufferPseudoSourceValue : public PseudoSourceValue {
public:
  explicit AMDGPUBufferPseudoSourceValue(const TargetInstrInfo &TII) :
    PseudoSourceValue(PseudoSourceValue::TargetCustom, TII) { }

  bool isConstant(const MachineFrameInfo *) const override {
    // This should probably be true for most images, but we will start by being
    // conservative.
    return false;
  }

  bool isAliased(const MachineFrameInfo *) const override {
    return true;
  }

  bool mayAlias(const MachineFrameInfo *) const override {
    return true;
  }
};

/// This class keeps track of the SPI_SP_INPUT_ADDR config register, which
/// tells the hardware which interpolation parameters to load.
class SIMachineFunctionInfo final : public AMDGPUMachineFunction {
  unsigned TIDReg = AMDGPU::NoRegister;

  // Registers that may be reserved for spilling purposes. These may be the same
  // as the input registers.
  unsigned ScratchRSrcReg = AMDGPU::PRIVATE_RSRC_REG;
  unsigned ScratchWaveOffsetReg = AMDGPU::SCRATCH_WAVE_OFFSET_REG;

  // This is the current function's incremented size from the kernel's scratch
  // wave offset register. For an entry function, this is exactly the same as
  // the ScratchWaveOffsetReg.
  unsigned FrameOffsetReg = AMDGPU::FP_REG;

  // Top of the stack SGPR offset derived from the ScratchWaveOffsetReg.
  unsigned StackPtrOffsetReg = AMDGPU::SP_REG;

  AMDGPUFunctionArgInfo ArgInfo;

  // Graphics info.
  unsigned PSInputAddr = 0;
  unsigned PSInputEnable = 0;

  /// Number of bytes of arguments this function has on the stack. If the callee
  /// is expected to restore the argument stack this should be a multiple of 16,
  /// all usable during a tail call.
  ///
  /// The alternative would forbid tail call optimisation in some cases: if we
  /// want to transfer control from a function with 8-bytes of stack-argument
  /// space to a function with 16-bytes then misalignment of this value would
  /// make a stack adjustment necessary, which could not be undone by the
  /// callee.
  unsigned BytesInStackArgArea = 0;

  bool ReturnsVoid = true;

  // A pair of default/requested minimum/maximum flat work group sizes.
  // Minimum - first, maximum - second.
  std::pair<unsigned, unsigned> FlatWorkGroupSizes = {0, 0};

  // A pair of default/requested minimum/maximum number of waves per execution
  // unit. Minimum - first, maximum - second.
  std::pair<unsigned, unsigned> WavesPerEU = {0, 0};

  // Stack object indices for work group IDs.
  std::array<int, 3> DebuggerWorkGroupIDStackObjectIndices = {{0, 0, 0}};

  // Stack object indices for work item IDs.
  std::array<int, 3> DebuggerWorkItemIDStackObjectIndices = {{0, 0, 0}};

  DenseMap<const Value *,
           std::unique_ptr<const AMDGPUBufferPseudoSourceValue>> BufferPSVs;
  DenseMap<const Value *,
           std::unique_ptr<const AMDGPUImagePseudoSourceValue>> ImagePSVs;

private:
  unsigned LDSWaveSpillSize = 0;
  unsigned NumUserSGPRs = 0;
  unsigned NumSystemSGPRs = 0;

  bool HasSpilledSGPRs = false;
  bool HasSpilledVGPRs = false;
  bool HasNonSpillStackObjects = false;
  bool IsStackRealigned = false;

  unsigned NumSpilledSGPRs = 0;
  unsigned NumSpilledVGPRs = 0;

  // Feature bits required for inputs passed in user SGPRs.
  bool PrivateSegmentBuffer : 1;
  bool DispatchPtr : 1;
  bool QueuePtr : 1;
  bool KernargSegmentPtr : 1;
  bool DispatchID : 1;
  bool FlatScratchInit : 1;

  // Feature bits required for inputs passed in system SGPRs.
  bool WorkGroupIDX : 1; // Always initialized.
  bool WorkGroupIDY : 1;
  bool WorkGroupIDZ : 1;
  bool WorkGroupInfo : 1;
  bool PrivateSegmentWaveByteOffset : 1;

  bool WorkItemIDX : 1; // Always initialized.
  bool WorkItemIDY : 1;
  bool WorkItemIDZ : 1;

  // Private memory buffer
  // Compute directly in sgpr[0:1]
  // Other shaders indirect 64-bits at sgpr[0:1]
  bool ImplicitBufferPtr : 1;

  // Pointer to where the ABI inserts special kernel arguments separate from the
  // user arguments. This is an offset from the KernargSegmentPtr.
  bool ImplicitArgPtr : 1;

  // The hard-wired high half of the address of the global information table
  // for AMDPAL OS type. 0xffffffff represents no hard-wired high half, since
  // current hardware only allows a 16 bit value.
  unsigned GITPtrHigh;

  unsigned HighBitsOf32BitAddress;

  // Current recorded maximum possible occupancy.
  unsigned Occupancy;

  MCPhysReg getNextUserSGPR() const;

  MCPhysReg getNextSystemSGPR() const;

public:
  struct SpilledReg {
    unsigned VGPR = 0;
    int Lane = -1;

    SpilledReg() = default;
    SpilledReg(unsigned R, int L) : VGPR (R), Lane (L) {}

    bool hasLane() { return Lane != -1;}
    bool hasReg() { return VGPR != 0;}
  };

  struct SGPRSpillVGPRCSR {
    // VGPR used for SGPR spills
    unsigned VGPR;

    // If the VGPR is a CSR, the stack slot used to save/restore it in the
    // prolog/epilog.
    Optional<int> FI;

    SGPRSpillVGPRCSR(unsigned V, Optional<int> F) : VGPR(V), FI(F) {}
  };

private:
  // SGPR->VGPR spilling support.
  using SpillRegMask = std::pair<unsigned, unsigned>;

  // Track VGPR + wave index for each subregister of the SGPR spilled to
  // frameindex key.
  DenseMap<int, std::vector<SpilledReg>> SGPRToVGPRSpills;
  unsigned NumVGPRSpillLanes = 0;
  SmallVector<SGPRSpillVGPRCSR, 2> SpillVGPRs;

public:
  SIMachineFunctionInfo(const MachineFunction &MF);

  ArrayRef<SpilledReg> getSGPRToVGPRSpills(int FrameIndex) const {
    auto I = SGPRToVGPRSpills.find(FrameIndex);
    return (I == SGPRToVGPRSpills.end()) ?
      ArrayRef<SpilledReg>() : makeArrayRef(I->second);
  }

  ArrayRef<SGPRSpillVGPRCSR> getSGPRSpillVGPRs() const {
    return SpillVGPRs;
  }

  bool allocateSGPRSpillToVGPR(MachineFunction &MF, int FI);
  void removeSGPRToVGPRFrameIndices(MachineFrameInfo &MFI);

  bool hasCalculatedTID() const { return TIDReg != 0; };
  unsigned getTIDReg() const { return TIDReg; };
  void setTIDReg(unsigned Reg) { TIDReg = Reg; }

  unsigned getBytesInStackArgArea() const {
    return BytesInStackArgArea;
  }

  void setBytesInStackArgArea(unsigned Bytes) {
    BytesInStackArgArea = Bytes;
  }

  // Add user SGPRs.
  unsigned addPrivateSegmentBuffer(const SIRegisterInfo &TRI);
  unsigned addDispatchPtr(const SIRegisterInfo &TRI);
  unsigned addQueuePtr(const SIRegisterInfo &TRI);
  unsigned addKernargSegmentPtr(const SIRegisterInfo &TRI);
  unsigned addDispatchID(const SIRegisterInfo &TRI);
  unsigned addFlatScratchInit(const SIRegisterInfo &TRI);
  unsigned addImplicitBufferPtr(const SIRegisterInfo &TRI);

  // Add system SGPRs.
  unsigned addWorkGroupIDX() {
    ArgInfo.WorkGroupIDX = ArgDescriptor::createRegister(getNextSystemSGPR());
    NumSystemSGPRs += 1;
    return ArgInfo.WorkGroupIDX.getRegister();
  }

  unsigned addWorkGroupIDY() {
    ArgInfo.WorkGroupIDY = ArgDescriptor::createRegister(getNextSystemSGPR());
    NumSystemSGPRs += 1;
    return ArgInfo.WorkGroupIDY.getRegister();
  }

  unsigned addWorkGroupIDZ() {
    ArgInfo.WorkGroupIDZ = ArgDescriptor::createRegister(getNextSystemSGPR());
    NumSystemSGPRs += 1;
    return ArgInfo.WorkGroupIDZ.getRegister();
  }

  unsigned addWorkGroupInfo() {
    ArgInfo.WorkGroupInfo = ArgDescriptor::createRegister(getNextSystemSGPR());
    NumSystemSGPRs += 1;
    return ArgInfo.WorkGroupInfo.getRegister();
  }

  // Add special VGPR inputs
  void setWorkItemIDX(ArgDescriptor Arg) {
    ArgInfo.WorkItemIDX = Arg;
  }

  void setWorkItemIDY(ArgDescriptor Arg) {
    ArgInfo.WorkItemIDY = Arg;
  }

  void setWorkItemIDZ(ArgDescriptor Arg) {
    ArgInfo.WorkItemIDZ = Arg;
  }

  unsigned addPrivateSegmentWaveByteOffset() {
    ArgInfo.PrivateSegmentWaveByteOffset
      = ArgDescriptor::createRegister(getNextSystemSGPR());
    NumSystemSGPRs += 1;
    return ArgInfo.PrivateSegmentWaveByteOffset.getRegister();
  }

  void setPrivateSegmentWaveByteOffset(unsigned Reg) {
    ArgInfo.PrivateSegmentWaveByteOffset = ArgDescriptor::createRegister(Reg);
  }

  bool hasPrivateSegmentBuffer() const {
    return PrivateSegmentBuffer;
  }

  bool hasDispatchPtr() const {
    return DispatchPtr;
  }

  bool hasQueuePtr() const {
    return QueuePtr;
  }

  bool hasKernargSegmentPtr() const {
    return KernargSegmentPtr;
  }

  bool hasDispatchID() const {
    return DispatchID;
  }

  bool hasFlatScratchInit() const {
    return FlatScratchInit;
  }

  bool hasWorkGroupIDX() const {
    return WorkGroupIDX;
  }

  bool hasWorkGroupIDY() const {
    return WorkGroupIDY;
  }

  bool hasWorkGroupIDZ() const {
    return WorkGroupIDZ;
  }

  bool hasWorkGroupInfo() const {
    return WorkGroupInfo;
  }

  bool hasPrivateSegmentWaveByteOffset() const {
    return PrivateSegmentWaveByteOffset;
  }

  bool hasWorkItemIDX() const {
    return WorkItemIDX;
  }

  bool hasWorkItemIDY() const {
    return WorkItemIDY;
  }

  bool hasWorkItemIDZ() const {
    return WorkItemIDZ;
  }

  bool hasImplicitArgPtr() const {
    return ImplicitArgPtr;
  }

  bool hasImplicitBufferPtr() const {
    return ImplicitBufferPtr;
  }

  AMDGPUFunctionArgInfo &getArgInfo() {
    return ArgInfo;
  }

  const AMDGPUFunctionArgInfo &getArgInfo() const {
    return ArgInfo;
  }

  std::pair<const ArgDescriptor *, const TargetRegisterClass *>
  getPreloadedValue(AMDGPUFunctionArgInfo::PreloadedValue Value) const {
    return ArgInfo.getPreloadedValue(Value);
  }

  unsigned getPreloadedReg(AMDGPUFunctionArgInfo::PreloadedValue Value) const {
    return ArgInfo.getPreloadedValue(Value).first->getRegister();
  }

  unsigned getGITPtrHigh() const {
    return GITPtrHigh;
  }

  unsigned get32BitAddressHighBits() const {
    return HighBitsOf32BitAddress;
  }

  unsigned getNumUserSGPRs() const {
    return NumUserSGPRs;
  }

  unsigned getNumPreloadedSGPRs() const {
    return NumUserSGPRs + NumSystemSGPRs;
  }

  unsigned getPrivateSegmentWaveByteOffsetSystemSGPR() const {
    return ArgInfo.PrivateSegmentWaveByteOffset.getRegister();
  }

  /// Returns the physical register reserved for use as the resource
  /// descriptor for scratch accesses.
  unsigned getScratchRSrcReg() const {
    return ScratchRSrcReg;
  }

  void setScratchRSrcReg(unsigned Reg) {
    assert(Reg != 0 && "Should never be unset");
    ScratchRSrcReg = Reg;
  }

  unsigned getScratchWaveOffsetReg() const {
    return ScratchWaveOffsetReg;
  }

  unsigned getFrameOffsetReg() const {
    return FrameOffsetReg;
  }

  void setStackPtrOffsetReg(unsigned Reg) {
    assert(Reg != 0 && "Should never be unset");
    StackPtrOffsetReg = Reg;
  }

  // Note the unset value for this is AMDGPU::SP_REG rather than
  // NoRegister. This is mostly a workaround for MIR tests where state that
  // can't be directly computed from the function is not preserved in serialized
  // MIR.
  unsigned getStackPtrOffsetReg() const {
    return StackPtrOffsetReg;
  }

  void setScratchWaveOffsetReg(unsigned Reg) {
    assert(Reg != 0 && "Should never be unset");
    ScratchWaveOffsetReg = Reg;
    if (isEntryFunction())
      FrameOffsetReg = ScratchWaveOffsetReg;
  }

  unsigned getQueuePtrUserSGPR() const {
    return ArgInfo.QueuePtr.getRegister();
  }

  unsigned getImplicitBufferPtrUserSGPR() const {
    return ArgInfo.ImplicitBufferPtr.getRegister();
  }

  bool hasSpilledSGPRs() const {
    return HasSpilledSGPRs;
  }

  void setHasSpilledSGPRs(bool Spill = true) {
    HasSpilledSGPRs = Spill;
  }

  bool hasSpilledVGPRs() const {
    return HasSpilledVGPRs;
  }

  void setHasSpilledVGPRs(bool Spill = true) {
    HasSpilledVGPRs = Spill;
  }

  bool hasNonSpillStackObjects() const {
    return HasNonSpillStackObjects;
  }

  void setHasNonSpillStackObjects(bool StackObject = true) {
    HasNonSpillStackObjects = StackObject;
  }

  bool isStackRealigned() const {
    return IsStackRealigned;
  }

  void setIsStackRealigned(bool Realigned = true) {
    IsStackRealigned = Realigned;
  }

  unsigned getNumSpilledSGPRs() const {
    return NumSpilledSGPRs;
  }

  unsigned getNumSpilledVGPRs() const {
    return NumSpilledVGPRs;
  }

  void addToSpilledSGPRs(unsigned num) {
    NumSpilledSGPRs += num;
  }

  void addToSpilledVGPRs(unsigned num) {
    NumSpilledVGPRs += num;
  }

  unsigned getPSInputAddr() const {
    return PSInputAddr;
  }

  unsigned getPSInputEnable() const {
    return PSInputEnable;
  }

  bool isPSInputAllocated(unsigned Index) const {
    return PSInputAddr & (1 << Index);
  }

  void markPSInputAllocated(unsigned Index) {
    PSInputAddr |= 1 << Index;
  }

  void markPSInputEnabled(unsigned Index) {
    PSInputEnable |= 1 << Index;
  }

  bool returnsVoid() const {
    return ReturnsVoid;
  }

  void setIfReturnsVoid(bool Value) {
    ReturnsVoid = Value;
  }

  /// \returns A pair of default/requested minimum/maximum flat work group sizes
  /// for this function.
  std::pair<unsigned, unsigned> getFlatWorkGroupSizes() const {
    return FlatWorkGroupSizes;
  }

  /// \returns Default/requested minimum flat work group size for this function.
  unsigned getMinFlatWorkGroupSize() const {
    return FlatWorkGroupSizes.first;
  }

  /// \returns Default/requested maximum flat work group size for this function.
  unsigned getMaxFlatWorkGroupSize() const {
    return FlatWorkGroupSizes.second;
  }

  /// \returns A pair of default/requested minimum/maximum number of waves per
  /// execution unit.
  std::pair<unsigned, unsigned> getWavesPerEU() const {
    return WavesPerEU;
  }

  /// \returns Default/requested minimum number of waves per execution unit.
  unsigned getMinWavesPerEU() const {
    return WavesPerEU.first;
  }

  /// \returns Default/requested maximum number of waves per execution unit.
  unsigned getMaxWavesPerEU() const {
    return WavesPerEU.second;
  }

  /// \returns Stack object index for \p Dim's work group ID.
  int getDebuggerWorkGroupIDStackObjectIndex(unsigned Dim) const {
    assert(Dim < 3);
    return DebuggerWorkGroupIDStackObjectIndices[Dim];
  }

  /// Sets stack object index for \p Dim's work group ID to \p ObjectIdx.
  void setDebuggerWorkGroupIDStackObjectIndex(unsigned Dim, int ObjectIdx) {
    assert(Dim < 3);
    DebuggerWorkGroupIDStackObjectIndices[Dim] = ObjectIdx;
  }

  /// \returns Stack object index for \p Dim's work item ID.
  int getDebuggerWorkItemIDStackObjectIndex(unsigned Dim) const {
    assert(Dim < 3);
    return DebuggerWorkItemIDStackObjectIndices[Dim];
  }

  /// Sets stack object index for \p Dim's work item ID to \p ObjectIdx.
  void setDebuggerWorkItemIDStackObjectIndex(unsigned Dim, int ObjectIdx) {
    assert(Dim < 3);
    DebuggerWorkItemIDStackObjectIndices[Dim] = ObjectIdx;
  }

  /// \returns SGPR used for \p Dim's work group ID.
  unsigned getWorkGroupIDSGPR(unsigned Dim) const {
    switch (Dim) {
    case 0:
      assert(hasWorkGroupIDX());
      return ArgInfo.WorkGroupIDX.getRegister();
    case 1:
      assert(hasWorkGroupIDY());
      return ArgInfo.WorkGroupIDY.getRegister();
    case 2:
      assert(hasWorkGroupIDZ());
      return ArgInfo.WorkGroupIDZ.getRegister();
    }
    llvm_unreachable("unexpected dimension");
  }

  /// \returns VGPR used for \p Dim' work item ID.
  unsigned getWorkItemIDVGPR(unsigned Dim) const;

  unsigned getLDSWaveSpillSize() const {
    return LDSWaveSpillSize;
  }

  const AMDGPUBufferPseudoSourceValue *getBufferPSV(const SIInstrInfo &TII,
                                                    const Value *BufferRsrc) {
    assert(BufferRsrc);
    auto PSV = BufferPSVs.try_emplace(
      BufferRsrc,
      llvm::make_unique<AMDGPUBufferPseudoSourceValue>(TII));
    return PSV.first->second.get();
  }

  const AMDGPUImagePseudoSourceValue *getImagePSV(const SIInstrInfo &TII,
                                                  const Value *ImgRsrc) {
    assert(ImgRsrc);
    auto PSV = ImagePSVs.try_emplace(
      ImgRsrc,
      llvm::make_unique<AMDGPUImagePseudoSourceValue>(TII));
    return PSV.first->second.get();
  }

  unsigned getOccupancy() const {
    return Occupancy;
  }

  unsigned getMinAllowedOccupancy() const {
    if (!isMemoryBound() && !needsWaveLimiter())
      return Occupancy;
    return (Occupancy < 4) ? Occupancy : 4;
  }

  void limitOccupancy(const MachineFunction &MF);

  void limitOccupancy(unsigned Limit) {
    if (Occupancy > Limit)
      Occupancy = Limit;
  }

  void increaseOccupancy(const MachineFunction &MF, unsigned Limit) {
    if (Occupancy < Limit)
      Occupancy = Limit;
    limitOccupancy(MF);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_SIMACHINEFUNCTIONINFO_H
