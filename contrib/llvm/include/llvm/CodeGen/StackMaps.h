//===- StackMaps.h - StackMaps ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_STACKMAPS_H
#define LLVM_CODEGEN_STACKMAPS_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Debug.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

namespace llvm {

class AsmPrinter;
class MCExpr;
class MCStreamer;
class raw_ostream;
class TargetRegisterInfo;

/// MI-level stackmap operands.
///
/// MI stackmap operations take the form:
/// <id>, <numBytes>, live args...
class StackMapOpers {
public:
  /// Enumerate the meta operands.
  enum { IDPos, NBytesPos };

private:
  const MachineInstr* MI;

public:
  explicit StackMapOpers(const MachineInstr *MI);

  /// Return the ID for the given stackmap
  uint64_t getID() const { return MI->getOperand(IDPos).getImm(); }

  /// Return the number of patchable bytes the given stackmap should emit.
  uint32_t getNumPatchBytes() const {
    return MI->getOperand(NBytesPos).getImm();
  }

  /// Get the operand index of the variable list of non-argument operands.
  /// These hold the "live state".
  unsigned getVarIdx() const {
    // Skip ID, nShadowBytes.
    return 2;
  }
};

/// MI-level patchpoint operands.
///
/// MI patchpoint operations take the form:
/// [<def>], <id>, <numBytes>, <target>, <numArgs>, <cc>, ...
///
/// IR patchpoint intrinsics do not have the <cc> operand because calling
/// convention is part of the subclass data.
///
/// SD patchpoint nodes do not have a def operand because it is part of the
/// SDValue.
///
/// Patchpoints following the anyregcc convention are handled specially. For
/// these, the stack map also records the location of the return value and
/// arguments.
class PatchPointOpers {
public:
  /// Enumerate the meta operands.
  enum { IDPos, NBytesPos, TargetPos, NArgPos, CCPos, MetaEnd };

private:
  const MachineInstr *MI;
  bool HasDef;

  unsigned getMetaIdx(unsigned Pos = 0) const {
    assert(Pos < MetaEnd && "Meta operand index out of range.");
    return (HasDef ? 1 : 0) + Pos;
  }

  const MachineOperand &getMetaOper(unsigned Pos) const {
    return MI->getOperand(getMetaIdx(Pos));
  }

public:
  explicit PatchPointOpers(const MachineInstr *MI);

  bool isAnyReg() const { return (getCallingConv() == CallingConv::AnyReg); }
  bool hasDef() const { return HasDef; }

  /// Return the ID for the given patchpoint.
  uint64_t getID() const { return getMetaOper(IDPos).getImm(); }

  /// Return the number of patchable bytes the given patchpoint should emit.
  uint32_t getNumPatchBytes() const {
    return getMetaOper(NBytesPos).getImm();
  }

  /// Returns the target of the underlying call.
  const MachineOperand &getCallTarget() const {
    return getMetaOper(TargetPos);
  }

  /// Returns the calling convention
  CallingConv::ID getCallingConv() const {
    return getMetaOper(CCPos).getImm();
  }

  unsigned getArgIdx() const { return getMetaIdx() + MetaEnd; }

  /// Return the number of call arguments
  uint32_t getNumCallArgs() const {
    return MI->getOperand(getMetaIdx(NArgPos)).getImm();
  }

  /// Get the operand index of the variable list of non-argument operands.
  /// These hold the "live state".
  unsigned getVarIdx() const {
    return getMetaIdx() + MetaEnd + getNumCallArgs();
  }

  /// Get the index at which stack map locations will be recorded.
  /// Arguments are not recorded unless the anyregcc convention is used.
  unsigned getStackMapStartIdx() const {
    if (isAnyReg())
      return getArgIdx();
    return getVarIdx();
  }

  /// Get the next scratch register operand index.
  unsigned getNextScratchIdx(unsigned StartIdx = 0) const;
};

/// MI-level Statepoint operands
///
/// Statepoint operands take the form:
///   <id>, <num patch bytes >, <num call arguments>, <call target>,
///   [call arguments...],
///   <StackMaps::ConstantOp>, <calling convention>,
///   <StackMaps::ConstantOp>, <statepoint flags>,
///   <StackMaps::ConstantOp>, <num deopt args>, [deopt args...],
///   <gc base/derived pairs...> <gc allocas...>
/// Note that the last two sets of arguments are not currently length
///   prefixed.
class StatepointOpers {
  // TODO:: we should change the STATEPOINT representation so that CC and
  // Flags should be part of meta operands, with args and deopt operands, and
  // gc operands all prefixed by their length and a type code. This would be
  // much more consistent.
public:
  // These values are aboolute offsets into the operands of the statepoint
  // instruction.
  enum { IDPos, NBytesPos, NCallArgsPos, CallTargetPos, MetaEnd };

  // These values are relative offests from the start of the statepoint meta
  // arguments (i.e. the end of the call arguments).
  enum { CCOffset = 1, FlagsOffset = 3, NumDeoptOperandsOffset = 5 };

  explicit StatepointOpers(const MachineInstr *MI) : MI(MI) {}

  /// Get starting index of non call related arguments
  /// (calling convention, statepoint flags, vm state and gc state).
  unsigned getVarIdx() const {
    return MI->getOperand(NCallArgsPos).getImm() + MetaEnd;
  }

  /// Return the ID for the given statepoint.
  uint64_t getID() const { return MI->getOperand(IDPos).getImm(); }

  /// Return the number of patchable bytes the given statepoint should emit.
  uint32_t getNumPatchBytes() const {
    return MI->getOperand(NBytesPos).getImm();
  }

  /// Returns the target of the underlying call.
  const MachineOperand &getCallTarget() const {
    return MI->getOperand(CallTargetPos);
  }

private:
  const MachineInstr *MI;
};

class StackMaps {
public:
  struct Location {
    enum LocationType {
      Unprocessed,
      Register,
      Direct,
      Indirect,
      Constant,
      ConstantIndex
    };
    LocationType Type = Unprocessed;
    unsigned Size = 0;
    unsigned Reg = 0;
    int64_t Offset = 0;

    Location() = default;
    Location(LocationType Type, unsigned Size, unsigned Reg, int64_t Offset)
        : Type(Type), Size(Size), Reg(Reg), Offset(Offset) {}
  };

  struct LiveOutReg {
    unsigned short Reg = 0;
    unsigned short DwarfRegNum = 0;
    unsigned short Size = 0;

    LiveOutReg() = default;
    LiveOutReg(unsigned short Reg, unsigned short DwarfRegNum,
               unsigned short Size)
        : Reg(Reg), DwarfRegNum(DwarfRegNum), Size(Size) {}
  };

  // OpTypes are used to encode information about the following logical
  // operand (which may consist of several MachineOperands) for the
  // OpParser.
  using OpType = enum { DirectMemRefOp, IndirectMemRefOp, ConstantOp };

  StackMaps(AsmPrinter &AP);

  void reset() {
    CSInfos.clear();
    ConstPool.clear();
    FnInfos.clear();
  }

  using LocationVec = SmallVector<Location, 8>;
  using LiveOutVec = SmallVector<LiveOutReg, 8>;
  using ConstantPool = MapVector<uint64_t, uint64_t>;

  struct FunctionInfo {
    uint64_t StackSize = 0;
    uint64_t RecordCount = 1;

    FunctionInfo() = default;
    explicit FunctionInfo(uint64_t StackSize) : StackSize(StackSize) {}
  };

  struct CallsiteInfo {
    const MCExpr *CSOffsetExpr = nullptr;
    uint64_t ID = 0;
    LocationVec Locations;
    LiveOutVec LiveOuts;

    CallsiteInfo() = default;
    CallsiteInfo(const MCExpr *CSOffsetExpr, uint64_t ID,
                 LocationVec &&Locations, LiveOutVec &&LiveOuts)
        : CSOffsetExpr(CSOffsetExpr), ID(ID), Locations(std::move(Locations)),
          LiveOuts(std::move(LiveOuts)) {}
  };

  using FnInfoMap = MapVector<const MCSymbol *, FunctionInfo>;
  using CallsiteInfoList = std::vector<CallsiteInfo>;

  /// Generate a stackmap record for a stackmap instruction.
  ///
  /// MI must be a raw STACKMAP, not a PATCHPOINT.
  void recordStackMap(const MachineInstr &MI);

  /// Generate a stackmap record for a patchpoint instruction.
  void recordPatchPoint(const MachineInstr &MI);

  /// Generate a stackmap record for a statepoint instruction.
  void recordStatepoint(const MachineInstr &MI);

  /// If there is any stack map data, create a stack map section and serialize
  /// the map info into it. This clears the stack map data structures
  /// afterwards.
  void serializeToStackMapSection();

  /// Get call site info.
  CallsiteInfoList &getCSInfos() { return CSInfos; }

  /// Get function info.
  FnInfoMap &getFnInfos() { return FnInfos; }

private:
  static const char *WSMP;

  AsmPrinter &AP;
  CallsiteInfoList CSInfos;
  ConstantPool ConstPool;
  FnInfoMap FnInfos;

  MachineInstr::const_mop_iterator
  parseOperand(MachineInstr::const_mop_iterator MOI,
               MachineInstr::const_mop_iterator MOE, LocationVec &Locs,
               LiveOutVec &LiveOuts) const;

  /// Create a live-out register record for the given register @p Reg.
  LiveOutReg createLiveOutReg(unsigned Reg,
                              const TargetRegisterInfo *TRI) const;

  /// Parse the register live-out mask and return a vector of live-out
  /// registers that need to be recorded in the stackmap.
  LiveOutVec parseRegisterLiveOutMask(const uint32_t *Mask) const;

  /// This should be called by the MC lowering code _immediately_ before
  /// lowering the MI to an MCInst. It records where the operands for the
  /// instruction are stored, and outputs a label to record the offset of
  /// the call from the start of the text section. In special cases (e.g. AnyReg
  /// calling convention) the return register is also recorded if requested.
  void recordStackMapOpers(const MachineInstr &MI, uint64_t ID,
                           MachineInstr::const_mop_iterator MOI,
                           MachineInstr::const_mop_iterator MOE,
                           bool recordResult = false);

  /// Emit the stackmap header.
  void emitStackmapHeader(MCStreamer &OS);

  /// Emit the function frame record for each function.
  void emitFunctionFrameRecords(MCStreamer &OS);

  /// Emit the constant pool.
  void emitConstantPoolEntries(MCStreamer &OS);

  /// Emit the callsite info for each stackmap/patchpoint intrinsic call.
  void emitCallsiteEntries(MCStreamer &OS);

  void print(raw_ostream &OS);
  void debug() { print(dbgs()); }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_STACKMAPS_H
