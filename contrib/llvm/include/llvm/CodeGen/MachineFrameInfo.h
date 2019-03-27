//===-- CodeGen/MachineFrameInfo.h - Abstract Stack Frame Rep. --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The file defines the MachineFrameInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEFRAMEINFO_H
#define LLVM_CODEGEN_MACHINEFRAMEINFO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/DataTypes.h"
#include <cassert>
#include <vector>

namespace llvm {
class raw_ostream;
class MachineFunction;
class MachineBasicBlock;
class BitVector;
class AllocaInst;

/// The CalleeSavedInfo class tracks the information need to locate where a
/// callee saved register is in the current frame.
/// Callee saved reg can also be saved to a different register rather than
/// on the stack by setting DstReg instead of FrameIdx.
class CalleeSavedInfo {
  unsigned Reg;
  union {
    int FrameIdx;
    unsigned DstReg;
  };
  /// Flag indicating whether the register is actually restored in the epilog.
  /// In most cases, if a register is saved, it is also restored. There are
  /// some situations, though, when this is not the case. For example, the
  /// LR register on ARM is usually saved, but on exit from the function its
  /// saved value may be loaded directly into PC. Since liveness tracking of
  /// physical registers treats callee-saved registers are live outside of
  /// the function, LR would be treated as live-on-exit, even though in these
  /// scenarios it is not. This flag is added to indicate that the saved
  /// register described by this object is not restored in the epilog.
  /// The long-term solution is to model the liveness of callee-saved registers
  /// by implicit uses on the return instructions, however, the required
  /// changes in the ARM backend would be quite extensive.
  bool Restored;
  /// Flag indicating whether the register is spilled to stack or another
  /// register.
  bool SpilledToReg;

public:
  explicit CalleeSavedInfo(unsigned R, int FI = 0)
  : Reg(R), FrameIdx(FI), Restored(true), SpilledToReg(false) {}

  // Accessors.
  unsigned getReg()                        const { return Reg; }
  int getFrameIdx()                        const { return FrameIdx; }
  unsigned getDstReg()                     const { return DstReg; }
  void setFrameIdx(int FI) {
    FrameIdx = FI;
    SpilledToReg = false;
  }
  void setDstReg(unsigned SpillReg) {
    DstReg = SpillReg;
    SpilledToReg = true;
  }
  bool isRestored()                        const { return Restored; }
  void setRestored(bool R)                       { Restored = R; }
  bool isSpilledToReg()                    const { return SpilledToReg; }
};

/// The MachineFrameInfo class represents an abstract stack frame until
/// prolog/epilog code is inserted.  This class is key to allowing stack frame
/// representation optimizations, such as frame pointer elimination.  It also
/// allows more mundane (but still important) optimizations, such as reordering
/// of abstract objects on the stack frame.
///
/// To support this, the class assigns unique integer identifiers to stack
/// objects requested clients.  These identifiers are negative integers for
/// fixed stack objects (such as arguments passed on the stack) or nonnegative
/// for objects that may be reordered.  Instructions which refer to stack
/// objects use a special MO_FrameIndex operand to represent these frame
/// indexes.
///
/// Because this class keeps track of all references to the stack frame, it
/// knows when a variable sized object is allocated on the stack.  This is the
/// sole condition which prevents frame pointer elimination, which is an
/// important optimization on register-poor architectures.  Because original
/// variable sized alloca's in the source program are the only source of
/// variable sized stack objects, it is safe to decide whether there will be
/// any variable sized objects before all stack objects are known (for
/// example, register allocator spill code never needs variable sized
/// objects).
///
/// When prolog/epilog code emission is performed, the final stack frame is
/// built and the machine instructions are modified to refer to the actual
/// stack offsets of the object, eliminating all MO_FrameIndex operands from
/// the program.
///
/// Abstract Stack Frame Information
class MachineFrameInfo {
public:
  /// Stack Smashing Protection (SSP) rules require that vulnerable stack
  /// allocations are located close the stack protector.
  enum SSPLayoutKind {
    SSPLK_None,       ///< Did not trigger a stack protector.  No effect on data
                      ///< layout.
    SSPLK_LargeArray, ///< Array or nested array >= SSP-buffer-size.  Closest
                      ///< to the stack protector.
    SSPLK_SmallArray, ///< Array or nested array < SSP-buffer-size. 2nd closest
                      ///< to the stack protector.
    SSPLK_AddrOf      ///< The address of this allocation is exposed and
                      ///< triggered protection.  3rd closest to the protector.
  };

private:
  // Represent a single object allocated on the stack.
  struct StackObject {
    // The offset of this object from the stack pointer on entry to
    // the function.  This field has no meaning for a variable sized element.
    int64_t SPOffset;

    // The size of this object on the stack. 0 means a variable sized object,
    // ~0ULL means a dead object.
    uint64_t Size;

    // The required alignment of this stack slot.
    unsigned Alignment;

    // If true, the value of the stack object is set before
    // entering the function and is not modified inside the function. By
    // default, fixed objects are immutable unless marked otherwise.
    bool isImmutable;

    // If true the stack object is used as spill slot. It
    // cannot alias any other memory objects.
    bool isSpillSlot;

    /// If true, this stack slot is used to spill a value (could be deopt
    /// and/or GC related) over a statepoint. We know that the address of the
    /// slot can't alias any LLVM IR value.  This is very similar to a Spill
    /// Slot, but is created by statepoint lowering is SelectionDAG, not the
    /// register allocator.
    bool isStatepointSpillSlot = false;

    /// Identifier for stack memory type analagous to address space. If this is
    /// non-0, the meaning is target defined. Offsets cannot be directly
    /// compared between objects with different stack IDs. The object may not
    /// necessarily reside in the same contiguous memory block as other stack
    /// objects. Objects with differing stack IDs should not be merged or
    /// replaced substituted for each other.
    //
    /// It is assumed a target uses consecutive, increasing stack IDs starting
    /// from 1.
    uint8_t StackID;

    /// If this stack object is originated from an Alloca instruction
    /// this value saves the original IR allocation. Can be NULL.
    const AllocaInst *Alloca;

    // If true, the object was mapped into the local frame
    // block and doesn't need additional handling for allocation beyond that.
    bool PreAllocated = false;

    // If true, an LLVM IR value might point to this object.
    // Normally, spill slots and fixed-offset objects don't alias IR-accessible
    // objects, but there are exceptions (on PowerPC, for example, some byval
    // arguments have ABI-prescribed offsets).
    bool isAliased;

    /// If true, the object has been zero-extended.
    bool isZExt = false;

    /// If true, the object has been zero-extended.
    bool isSExt = false;

    uint8_t SSPLayout;

    StackObject(uint64_t Size, unsigned Alignment, int64_t SPOffset,
                bool IsImmutable, bool IsSpillSlot, const AllocaInst *Alloca,
                bool IsAliased, uint8_t StackID = 0)
      : SPOffset(SPOffset), Size(Size), Alignment(Alignment),
        isImmutable(IsImmutable), isSpillSlot(IsSpillSlot),
        StackID(StackID), Alloca(Alloca), isAliased(IsAliased),
        SSPLayout(SSPLK_None) {}
  };

  /// The alignment of the stack.
  unsigned StackAlignment;

  /// Can the stack be realigned. This can be false if the target does not
  /// support stack realignment, or if the user asks us not to realign the
  /// stack. In this situation, overaligned allocas are all treated as dynamic
  /// allocations and the target must handle them as part of DYNAMIC_STACKALLOC
  /// lowering. All non-alloca stack objects have their alignment clamped to the
  /// base ABI stack alignment.
  /// FIXME: There is room for improvement in this case, in terms of
  /// grouping overaligned allocas into a "secondary stack frame" and
  /// then only use a single alloca to allocate this frame and only a
  /// single virtual register to access it. Currently, without such an
  /// optimization, each such alloca gets its own dynamic realignment.
  bool StackRealignable;

  /// Whether the function has the \c alignstack attribute.
  bool ForcedRealign;

  /// The list of stack objects allocated.
  std::vector<StackObject> Objects;

  /// This contains the number of fixed objects contained on
  /// the stack.  Because fixed objects are stored at a negative index in the
  /// Objects list, this is also the index to the 0th object in the list.
  unsigned NumFixedObjects = 0;

  /// This boolean keeps track of whether any variable
  /// sized objects have been allocated yet.
  bool HasVarSizedObjects = false;

  /// This boolean keeps track of whether there is a call
  /// to builtin \@llvm.frameaddress.
  bool FrameAddressTaken = false;

  /// This boolean keeps track of whether there is a call
  /// to builtin \@llvm.returnaddress.
  bool ReturnAddressTaken = false;

  /// This boolean keeps track of whether there is a call
  /// to builtin \@llvm.experimental.stackmap.
  bool HasStackMap = false;

  /// This boolean keeps track of whether there is a call
  /// to builtin \@llvm.experimental.patchpoint.
  bool HasPatchPoint = false;

  /// The prolog/epilog code inserter calculates the final stack
  /// offsets for all of the fixed size objects, updating the Objects list
  /// above.  It then updates StackSize to contain the number of bytes that need
  /// to be allocated on entry to the function.
  uint64_t StackSize = 0;

  /// The amount that a frame offset needs to be adjusted to
  /// have the actual offset from the stack/frame pointer.  The exact usage of
  /// this is target-dependent, but it is typically used to adjust between
  /// SP-relative and FP-relative offsets.  E.G., if objects are accessed via
  /// SP then OffsetAdjustment is zero; if FP is used, OffsetAdjustment is set
  /// to the distance between the initial SP and the value in FP.  For many
  /// targets, this value is only used when generating debug info (via
  /// TargetRegisterInfo::getFrameIndexReference); when generating code, the
  /// corresponding adjustments are performed directly.
  int OffsetAdjustment = 0;

  /// The prolog/epilog code inserter may process objects that require greater
  /// alignment than the default alignment the target provides.
  /// To handle this, MaxAlignment is set to the maximum alignment
  /// needed by the objects on the current frame.  If this is greater than the
  /// native alignment maintained by the compiler, dynamic alignment code will
  /// be needed.
  ///
  unsigned MaxAlignment = 0;

  /// Set to true if this function adjusts the stack -- e.g.,
  /// when calling another function. This is only valid during and after
  /// prolog/epilog code insertion.
  bool AdjustsStack = false;

  /// Set to true if this function has any function calls.
  bool HasCalls = false;

  /// The frame index for the stack protector.
  int StackProtectorIdx = -1;

  /// The frame index for the function context. Used for SjLj exceptions.
  int FunctionContextIdx = -1;

  /// This contains the size of the largest call frame if the target uses frame
  /// setup/destroy pseudo instructions (as defined in the TargetFrameInfo
  /// class).  This information is important for frame pointer elimination.
  /// It is only valid during and after prolog/epilog code insertion.
  unsigned MaxCallFrameSize = ~0u;

  /// The number of bytes of callee saved registers that the target wants to
  /// report for the current function in the CodeView S_FRAMEPROC record.
  unsigned CVBytesOfCalleeSavedRegisters = 0;

  /// The prolog/epilog code inserter fills in this vector with each
  /// callee saved register saved in either the frame or a different
  /// register.  Beyond its use by the prolog/ epilog code inserter,
  /// this data is used for debug info and exception handling.
  std::vector<CalleeSavedInfo> CSInfo;

  /// Has CSInfo been set yet?
  bool CSIValid = false;

  /// References to frame indices which are mapped
  /// into the local frame allocation block. <FrameIdx, LocalOffset>
  SmallVector<std::pair<int, int64_t>, 32> LocalFrameObjects;

  /// Size of the pre-allocated local frame block.
  int64_t LocalFrameSize = 0;

  /// Required alignment of the local object blob, which is the strictest
  /// alignment of any object in it.
  unsigned LocalFrameMaxAlign = 0;

  /// Whether the local object blob needs to be allocated together. If not,
  /// PEI should ignore the isPreAllocated flags on the stack objects and
  /// just allocate them normally.
  bool UseLocalStackAllocationBlock = false;

  /// True if the function dynamically adjusts the stack pointer through some
  /// opaque mechanism like inline assembly or Win32 EH.
  bool HasOpaqueSPAdjustment = false;

  /// True if the function contains operations which will lower down to
  /// instructions which manipulate the stack pointer.
  bool HasCopyImplyingStackAdjustment = false;

  /// True if the function contains a call to the llvm.vastart intrinsic.
  bool HasVAStart = false;

  /// True if this is a varargs function that contains a musttail call.
  bool HasMustTailInVarArgFunc = false;

  /// True if this function contains a tail call. If so immutable objects like
  /// function arguments are no longer so. A tail call *can* override fixed
  /// stack objects like arguments so we can't treat them as immutable.
  bool HasTailCall = false;

  /// Not null, if shrink-wrapping found a better place for the prologue.
  MachineBasicBlock *Save = nullptr;
  /// Not null, if shrink-wrapping found a better place for the epilogue.
  MachineBasicBlock *Restore = nullptr;

public:
  explicit MachineFrameInfo(unsigned StackAlignment, bool StackRealignable,
                            bool ForcedRealign)
      : StackAlignment(StackAlignment), StackRealignable(StackRealignable),
        ForcedRealign(ForcedRealign) {}

  /// Return true if there are any stack objects in this function.
  bool hasStackObjects() const { return !Objects.empty(); }

  /// This method may be called any time after instruction
  /// selection is complete to determine if the stack frame for this function
  /// contains any variable sized objects.
  bool hasVarSizedObjects() const { return HasVarSizedObjects; }

  /// Return the index for the stack protector object.
  int getStackProtectorIndex() const { return StackProtectorIdx; }
  void setStackProtectorIndex(int I) { StackProtectorIdx = I; }
  bool hasStackProtectorIndex() const { return StackProtectorIdx != -1; }

  /// Return the index for the function context object.
  /// This object is used for SjLj exceptions.
  int getFunctionContextIndex() const { return FunctionContextIdx; }
  void setFunctionContextIndex(int I) { FunctionContextIdx = I; }

  /// This method may be called any time after instruction
  /// selection is complete to determine if there is a call to
  /// \@llvm.frameaddress in this function.
  bool isFrameAddressTaken() const { return FrameAddressTaken; }
  void setFrameAddressIsTaken(bool T) { FrameAddressTaken = T; }

  /// This method may be called any time after
  /// instruction selection is complete to determine if there is a call to
  /// \@llvm.returnaddress in this function.
  bool isReturnAddressTaken() const { return ReturnAddressTaken; }
  void setReturnAddressIsTaken(bool s) { ReturnAddressTaken = s; }

  /// This method may be called any time after instruction
  /// selection is complete to determine if there is a call to builtin
  /// \@llvm.experimental.stackmap.
  bool hasStackMap() const { return HasStackMap; }
  void setHasStackMap(bool s = true) { HasStackMap = s; }

  /// This method may be called any time after instruction
  /// selection is complete to determine if there is a call to builtin
  /// \@llvm.experimental.patchpoint.
  bool hasPatchPoint() const { return HasPatchPoint; }
  void setHasPatchPoint(bool s = true) { HasPatchPoint = s; }

  /// Return the minimum frame object index.
  int getObjectIndexBegin() const { return -NumFixedObjects; }

  /// Return one past the maximum frame object index.
  int getObjectIndexEnd() const { return (int)Objects.size()-NumFixedObjects; }

  /// Return the number of fixed objects.
  unsigned getNumFixedObjects() const { return NumFixedObjects; }

  /// Return the number of objects.
  unsigned getNumObjects() const { return Objects.size(); }

  /// Map a frame index into the local object block
  void mapLocalFrameObject(int ObjectIndex, int64_t Offset) {
    LocalFrameObjects.push_back(std::pair<int, int64_t>(ObjectIndex, Offset));
    Objects[ObjectIndex + NumFixedObjects].PreAllocated = true;
  }

  /// Get the local offset mapping for a for an object.
  std::pair<int, int64_t> getLocalFrameObjectMap(int i) const {
    assert (i >= 0 && (unsigned)i < LocalFrameObjects.size() &&
            "Invalid local object reference!");
    return LocalFrameObjects[i];
  }

  /// Return the number of objects allocated into the local object block.
  int64_t getLocalFrameObjectCount() const { return LocalFrameObjects.size(); }

  /// Set the size of the local object blob.
  void setLocalFrameSize(int64_t sz) { LocalFrameSize = sz; }

  /// Get the size of the local object blob.
  int64_t getLocalFrameSize() const { return LocalFrameSize; }

  /// Required alignment of the local object blob,
  /// which is the strictest alignment of any object in it.
  void setLocalFrameMaxAlign(unsigned Align) { LocalFrameMaxAlign = Align; }

  /// Return the required alignment of the local object blob.
  unsigned getLocalFrameMaxAlign() const { return LocalFrameMaxAlign; }

  /// Get whether the local allocation blob should be allocated together or
  /// let PEI allocate the locals in it directly.
  bool getUseLocalStackAllocationBlock() const {
    return UseLocalStackAllocationBlock;
  }

  /// setUseLocalStackAllocationBlock - Set whether the local allocation blob
  /// should be allocated together or let PEI allocate the locals in it
  /// directly.
  void setUseLocalStackAllocationBlock(bool v) {
    UseLocalStackAllocationBlock = v;
  }

  /// Return true if the object was pre-allocated into the local block.
  bool isObjectPreAllocated(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].PreAllocated;
  }

  /// Return the size of the specified object.
  int64_t getObjectSize(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].Size;
  }

  /// Change the size of the specified stack object.
  void setObjectSize(int ObjectIdx, int64_t Size) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].Size = Size;
  }

  /// Return the alignment of the specified stack object.
  unsigned getObjectAlignment(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].Alignment;
  }

  /// setObjectAlignment - Change the alignment of the specified stack object.
  void setObjectAlignment(int ObjectIdx, unsigned Align) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].Alignment = Align;
    ensureMaxAlignment(Align);
  }

  /// Return the underlying Alloca of the specified
  /// stack object if it exists. Returns 0 if none exists.
  const AllocaInst* getObjectAllocation(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].Alloca;
  }

  /// Return the assigned stack offset of the specified object
  /// from the incoming stack pointer.
  int64_t getObjectOffset(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    assert(!isDeadObjectIndex(ObjectIdx) &&
           "Getting frame offset for a dead object?");
    return Objects[ObjectIdx+NumFixedObjects].SPOffset;
  }

  bool isObjectZExt(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isZExt;
  }

  void setObjectZExt(int ObjectIdx, bool IsZExt) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].isZExt = IsZExt;
  }

  bool isObjectSExt(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isSExt;
  }

  void setObjectSExt(int ObjectIdx, bool IsSExt) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].isSExt = IsSExt;
  }

  /// Set the stack frame offset of the specified object. The
  /// offset is relative to the stack pointer on entry to the function.
  void setObjectOffset(int ObjectIdx, int64_t SPOffset) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    assert(!isDeadObjectIndex(ObjectIdx) &&
           "Setting frame offset for a dead object?");
    Objects[ObjectIdx+NumFixedObjects].SPOffset = SPOffset;
  }

  SSPLayoutKind getObjectSSPLayout(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return (SSPLayoutKind)Objects[ObjectIdx+NumFixedObjects].SSPLayout;
  }

  void setObjectSSPLayout(int ObjectIdx, SSPLayoutKind Kind) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    assert(!isDeadObjectIndex(ObjectIdx) &&
           "Setting SSP layout for a dead object?");
    Objects[ObjectIdx+NumFixedObjects].SSPLayout = Kind;
  }

  /// Return the number of bytes that must be allocated to hold
  /// all of the fixed size frame objects.  This is only valid after
  /// Prolog/Epilog code insertion has finalized the stack frame layout.
  uint64_t getStackSize() const { return StackSize; }

  /// Set the size of the stack.
  void setStackSize(uint64_t Size) { StackSize = Size; }

  /// Estimate and return the size of the stack frame.
  unsigned estimateStackSize(const MachineFunction &MF) const;

  /// Return the correction for frame offsets.
  int getOffsetAdjustment() const { return OffsetAdjustment; }

  /// Set the correction for frame offsets.
  void setOffsetAdjustment(int Adj) { OffsetAdjustment = Adj; }

  /// Return the alignment in bytes that this function must be aligned to,
  /// which is greater than the default stack alignment provided by the target.
  unsigned getMaxAlignment() const { return MaxAlignment; }

  /// Make sure the function is at least Align bytes aligned.
  void ensureMaxAlignment(unsigned Align);

  /// Return true if this function adjusts the stack -- e.g.,
  /// when calling another function. This is only valid during and after
  /// prolog/epilog code insertion.
  bool adjustsStack() const { return AdjustsStack; }
  void setAdjustsStack(bool V) { AdjustsStack = V; }

  /// Return true if the current function has any function calls.
  bool hasCalls() const { return HasCalls; }
  void setHasCalls(bool V) { HasCalls = V; }

  /// Returns true if the function contains opaque dynamic stack adjustments.
  bool hasOpaqueSPAdjustment() const { return HasOpaqueSPAdjustment; }
  void setHasOpaqueSPAdjustment(bool B) { HasOpaqueSPAdjustment = B; }

  /// Returns true if the function contains operations which will lower down to
  /// instructions which manipulate the stack pointer.
  bool hasCopyImplyingStackAdjustment() const {
    return HasCopyImplyingStackAdjustment;
  }
  void setHasCopyImplyingStackAdjustment(bool B) {
    HasCopyImplyingStackAdjustment = B;
  }

  /// Returns true if the function calls the llvm.va_start intrinsic.
  bool hasVAStart() const { return HasVAStart; }
  void setHasVAStart(bool B) { HasVAStart = B; }

  /// Returns true if the function is variadic and contains a musttail call.
  bool hasMustTailInVarArgFunc() const { return HasMustTailInVarArgFunc; }
  void setHasMustTailInVarArgFunc(bool B) { HasMustTailInVarArgFunc = B; }

  /// Returns true if the function contains a tail call.
  bool hasTailCall() const { return HasTailCall; }
  void setHasTailCall() { HasTailCall = true; }

  /// Computes the maximum size of a callframe and the AdjustsStack property.
  /// This only works for targets defining
  /// TargetInstrInfo::getCallFrameSetupOpcode(), getCallFrameDestroyOpcode(),
  /// and getFrameSize().
  /// This is usually computed by the prologue epilogue inserter but some
  /// targets may call this to compute it earlier.
  void computeMaxCallFrameSize(const MachineFunction &MF);

  /// Return the maximum size of a call frame that must be
  /// allocated for an outgoing function call.  This is only available if
  /// CallFrameSetup/Destroy pseudo instructions are used by the target, and
  /// then only during or after prolog/epilog code insertion.
  ///
  unsigned getMaxCallFrameSize() const {
    // TODO: Enable this assert when targets are fixed.
    //assert(isMaxCallFrameSizeComputed() && "MaxCallFrameSize not computed yet");
    if (!isMaxCallFrameSizeComputed())
      return 0;
    return MaxCallFrameSize;
  }
  bool isMaxCallFrameSizeComputed() const {
    return MaxCallFrameSize != ~0u;
  }
  void setMaxCallFrameSize(unsigned S) { MaxCallFrameSize = S; }

  /// Returns how many bytes of callee-saved registers the target pushed in the
  /// prologue. Only used for debug info.
  unsigned getCVBytesOfCalleeSavedRegisters() const {
    return CVBytesOfCalleeSavedRegisters;
  }
  void setCVBytesOfCalleeSavedRegisters(unsigned S) {
    CVBytesOfCalleeSavedRegisters = S;
  }

  /// Create a new object at a fixed location on the stack.
  /// All fixed objects should be created before other objects are created for
  /// efficiency. By default, fixed objects are not pointed to by LLVM IR
  /// values. This returns an index with a negative value.
  int CreateFixedObject(uint64_t Size, int64_t SPOffset, bool IsImmutable,
                        bool isAliased = false);

  /// Create a spill slot at a fixed location on the stack.
  /// Returns an index with a negative value.
  int CreateFixedSpillStackObject(uint64_t Size, int64_t SPOffset,
                                  bool IsImmutable = false);

  /// Returns true if the specified index corresponds to a fixed stack object.
  bool isFixedObjectIndex(int ObjectIdx) const {
    return ObjectIdx < 0 && (ObjectIdx >= -(int)NumFixedObjects);
  }

  /// Returns true if the specified index corresponds
  /// to an object that might be pointed to by an LLVM IR value.
  bool isAliasedObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isAliased;
  }

  /// Returns true if the specified index corresponds to an immutable object.
  bool isImmutableObjectIndex(int ObjectIdx) const {
    // Tail calling functions can clobber their function arguments.
    if (HasTailCall)
      return false;
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isImmutable;
  }

  /// Marks the immutability of an object.
  void setIsImmutableObjectIndex(int ObjectIdx, bool IsImmutable) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].isImmutable = IsImmutable;
  }

  /// Returns true if the specified index corresponds to a spill slot.
  bool isSpillSlotObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isSpillSlot;
  }

  bool isStatepointSpillSlotObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isStatepointSpillSlot;
  }

  /// \see StackID
  uint8_t getStackID(int ObjectIdx) const {
    return Objects[ObjectIdx+NumFixedObjects].StackID;
  }

  /// \see StackID
  void setStackID(int ObjectIdx, uint8_t ID) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].StackID = ID;
  }

  /// Returns true if the specified index corresponds to a dead object.
  bool isDeadObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].Size == ~0ULL;
  }

  /// Returns true if the specified index corresponds to a variable sized
  /// object.
  bool isVariableSizedObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx + NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx + NumFixedObjects].Size == 0;
  }

  void markAsStatepointSpillSlotObjectIndex(int ObjectIdx) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].isStatepointSpillSlot = true;
    assert(isStatepointSpillSlotObjectIndex(ObjectIdx) && "inconsistent");
  }

  /// Create a new statically sized stack object, returning
  /// a nonnegative identifier to represent it.
  int CreateStackObject(uint64_t Size, unsigned Alignment, bool isSpillSlot,
                        const AllocaInst *Alloca = nullptr, uint8_t ID = 0);

  /// Create a new statically sized stack object that represents a spill slot,
  /// returning a nonnegative identifier to represent it.
  int CreateSpillStackObject(uint64_t Size, unsigned Alignment);

  /// Remove or mark dead a statically sized stack object.
  void RemoveStackObject(int ObjectIdx) {
    // Mark it dead.
    Objects[ObjectIdx+NumFixedObjects].Size = ~0ULL;
  }

  /// Notify the MachineFrameInfo object that a variable sized object has been
  /// created.  This must be created whenever a variable sized object is
  /// created, whether or not the index returned is actually used.
  int CreateVariableSizedObject(unsigned Alignment, const AllocaInst *Alloca);

  /// Returns a reference to call saved info vector for the current function.
  const std::vector<CalleeSavedInfo> &getCalleeSavedInfo() const {
    return CSInfo;
  }
  /// \copydoc getCalleeSavedInfo()
  std::vector<CalleeSavedInfo> &getCalleeSavedInfo() { return CSInfo; }

  /// Used by prolog/epilog inserter to set the function's callee saved
  /// information.
  void setCalleeSavedInfo(const std::vector<CalleeSavedInfo> &CSI) {
    CSInfo = CSI;
  }

  /// Has the callee saved info been calculated yet?
  bool isCalleeSavedInfoValid() const { return CSIValid; }

  void setCalleeSavedInfoValid(bool v) { CSIValid = v; }

  MachineBasicBlock *getSavePoint() const { return Save; }
  void setSavePoint(MachineBasicBlock *NewSave) { Save = NewSave; }
  MachineBasicBlock *getRestorePoint() const { return Restore; }
  void setRestorePoint(MachineBasicBlock *NewRestore) { Restore = NewRestore; }

  /// Return a set of physical registers that are pristine.
  ///
  /// Pristine registers hold a value that is useless to the current function,
  /// but that must be preserved - they are callee saved registers that are not
  /// saved.
  ///
  /// Before the PrologueEpilogueInserter has placed the CSR spill code, this
  /// method always returns an empty set.
  BitVector getPristineRegs(const MachineFunction &MF) const;

  /// Used by the MachineFunction printer to print information about
  /// stack objects. Implemented in MachineFunction.cpp.
  void print(const MachineFunction &MF, raw_ostream &OS) const;

  /// dump - Print the function to stderr.
  void dump(const MachineFunction &MF) const;
};

} // End llvm namespace

#endif
