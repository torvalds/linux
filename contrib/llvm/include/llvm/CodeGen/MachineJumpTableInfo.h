//===-- CodeGen/MachineJumpTableInfo.h - Abstract Jump Tables  --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The MachineJumpTableInfo class keeps track of jump tables referenced by
// lowered switch instructions in the MachineFunction.
//
// Instructions reference the address of these jump tables through the use of
// MO_JumpTableIndex values.  When emitting assembly or machine code, these
// virtual address references are converted to refer to the address of the
// function jump tables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEJUMPTABLEINFO_H
#define LLVM_CODEGEN_MACHINEJUMPTABLEINFO_H

#include "llvm/Support/Printable.h"
#include <cassert>
#include <vector>

namespace llvm {

class MachineBasicBlock;
class DataLayout;
class raw_ostream;

/// MachineJumpTableEntry - One jump table in the jump table info.
///
struct MachineJumpTableEntry {
  /// MBBs - The vector of basic blocks from which to create the jump table.
  std::vector<MachineBasicBlock*> MBBs;

  explicit MachineJumpTableEntry(const std::vector<MachineBasicBlock*> &M)
  : MBBs(M) {}
};

class MachineJumpTableInfo {
public:
  /// JTEntryKind - This enum indicates how each entry of the jump table is
  /// represented and emitted.
  enum JTEntryKind {
    /// EK_BlockAddress - Each entry is a plain address of block, e.g.:
    ///     .word LBB123
    EK_BlockAddress,

    /// EK_GPRel64BlockAddress - Each entry is an address of block, encoded
    /// with a relocation as gp-relative, e.g.:
    ///     .gpdword LBB123
    EK_GPRel64BlockAddress,

    /// EK_GPRel32BlockAddress - Each entry is an address of block, encoded
    /// with a relocation as gp-relative, e.g.:
    ///     .gprel32 LBB123
    EK_GPRel32BlockAddress,

    /// EK_LabelDifference32 - Each entry is the address of the block minus
    /// the address of the jump table.  This is used for PIC jump tables where
    /// gprel32 is not supported.  e.g.:
    ///      .word LBB123 - LJTI1_2
    /// If the .set directive is supported, this is emitted as:
    ///      .set L4_5_set_123, LBB123 - LJTI1_2
    ///      .word L4_5_set_123
    EK_LabelDifference32,

    /// EK_Inline - Jump table entries are emitted inline at their point of
    /// use. It is the responsibility of the target to emit the entries.
    EK_Inline,

    /// EK_Custom32 - Each entry is a 32-bit value that is custom lowered by the
    /// TargetLowering::LowerCustomJumpTableEntry hook.
    EK_Custom32
  };
private:
  JTEntryKind EntryKind;
  std::vector<MachineJumpTableEntry> JumpTables;
public:
  explicit MachineJumpTableInfo(JTEntryKind Kind): EntryKind(Kind) {}

  JTEntryKind getEntryKind() const { return EntryKind; }

  /// getEntrySize - Return the size of each entry in the jump table.
  unsigned getEntrySize(const DataLayout &TD) const;
  /// getEntryAlignment - Return the alignment of each entry in the jump table.
  unsigned getEntryAlignment(const DataLayout &TD) const;

  /// createJumpTableIndex - Create a new jump table.
  ///
  unsigned createJumpTableIndex(const std::vector<MachineBasicBlock*> &DestBBs);

  /// isEmpty - Return true if there are no jump tables.
  ///
  bool isEmpty() const { return JumpTables.empty(); }

  const std::vector<MachineJumpTableEntry> &getJumpTables() const {
    return JumpTables;
  }

  /// RemoveJumpTable - Mark the specific index as being dead.  This will
  /// prevent it from being emitted.
  void RemoveJumpTable(unsigned Idx) {
    JumpTables[Idx].MBBs.clear();
  }

  /// ReplaceMBBInJumpTables - If Old is the target of any jump tables, update
  /// the jump tables to branch to New instead.
  bool ReplaceMBBInJumpTables(MachineBasicBlock *Old, MachineBasicBlock *New);

  /// ReplaceMBBInJumpTable - If Old is a target of the jump tables, update
  /// the jump table to branch to New instead.
  bool ReplaceMBBInJumpTable(unsigned Idx, MachineBasicBlock *Old,
                             MachineBasicBlock *New);

  /// print - Used by the MachineFunction printer to print information about
  /// jump tables.  Implemented in MachineFunction.cpp
  ///
  void print(raw_ostream &OS) const;

  /// dump - Call to stderr.
  ///
  void dump() const;
};


/// Prints a jump table entry reference.
///
/// The format is:
///   %jump-table.5       - a jump table entry with index == 5.
///
/// Usage: OS << printJumpTableEntryReference(Idx) << '\n';
Printable printJumpTableEntryReference(unsigned Idx);

} // End llvm namespace

#endif
