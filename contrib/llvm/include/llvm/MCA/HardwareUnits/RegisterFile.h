//===--------------------- RegisterFile.h -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a register mapping file class.  This class is responsible
/// for managing hardware register files and the tracking of data dependencies
/// between registers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_REGISTER_FILE_H
#define LLVM_MCA_REGISTER_FILE_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MCA/HardwareUnits/HardwareUnit.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace mca {

class ReadState;
class WriteState;
class WriteRef;

/// Manages hardware register files, and tracks register definitions for
/// register renaming purposes.
class RegisterFile : public HardwareUnit {
  const MCRegisterInfo &MRI;

  // class RegisterMappingTracker is a  physical register file (PRF) descriptor.
  // There is one RegisterMappingTracker for every PRF definition in the
  // scheduling model.
  //
  // An instance of RegisterMappingTracker tracks the number of physical
  // registers available for renaming. It also tracks  the number of register
  // moves eliminated per cycle.
  struct RegisterMappingTracker {
    // The total number of physical registers that are available in this
    // register file for register renaming purpouses.  A value of zero for this
    // field means: this register file has an unbounded number of physical
    // registers.
    const unsigned NumPhysRegs;
    // Number of physical registers that are currently in use.
    unsigned NumUsedPhysRegs;

    // Maximum number of register moves that can be eliminated by this PRF every
    // cycle. A value of zero means that there is no limit in the number of
    // moves which can be eliminated every cycle.
    const unsigned MaxMoveEliminatedPerCycle;

    // Number of register moves eliminated during this cycle.
    //
    // This value is increased by one every time a register move is eliminated.
    // Every new cycle, this value is reset to zero.
    // A move can be eliminated only if MaxMoveEliminatedPerCycle is zero, or if
    // NumMoveEliminated is less than MaxMoveEliminatedPerCycle.
    unsigned NumMoveEliminated;

    // If set, move elimination is restricted to zero-register moves only.
    bool AllowZeroMoveEliminationOnly;

    RegisterMappingTracker(unsigned NumPhysRegisters,
                           unsigned MaxMoveEliminated = 0U,
                           bool AllowZeroMoveElimOnly = false)
        : NumPhysRegs(NumPhysRegisters), NumUsedPhysRegs(0),
          MaxMoveEliminatedPerCycle(MaxMoveEliminated), NumMoveEliminated(0U),
          AllowZeroMoveEliminationOnly(AllowZeroMoveElimOnly) {}
  };

  // A vector of register file descriptors.  This set always contains at least
  // one entry. Entry at index #0 is reserved.  That entry describes a register
  // file with an unbounded number of physical registers that "sees" all the
  // hardware registers declared by the target (i.e. all the register
  // definitions in the target specific `XYZRegisterInfo.td` - where `XYZ` is
  // the target name).
  //
  // Users can limit the number of physical registers that are available in
  // regsiter file #0 specifying command line flag `-register-file-size=<uint>`.
  SmallVector<RegisterMappingTracker, 4> RegisterFiles;

  // This type is used to propagate information about the owner of a register,
  // and the cost of allocating it in the PRF. Register cost is defined as the
  // number of physical registers consumed by the PRF to allocate a user
  // register.
  //
  // For example: on X86 BtVer2, a YMM register consumes 2 128-bit physical
  // registers. So, the cost of allocating a YMM register in BtVer2 is 2.
  using IndexPlusCostPairTy = std::pair<unsigned, unsigned>;

  // Struct RegisterRenamingInfo is used to map logical registers to register
  // files.
  //
  // There is a RegisterRenamingInfo object for every logical register defined
  // by the target. RegisteRenamingInfo objects are stored into vector
  // `RegisterMappings`, and MCPhysReg IDs can be used to reference
  // elements in that vector.
  //
  // Each RegisterRenamingInfo is owned by a PRF, and field `IndexPlusCost`
  // specifies both the owning PRF, as well as the number of physical registers
  // consumed at register renaming stage.
  //
  // Field `AllowMoveElimination` is set for registers that are used as
  // destination by optimizable register moves.
  //
  // Field `AliasRegID` is set by writes from register moves that have been
  // eliminated at register renaming stage. A move eliminated at register
  // renaming stage is effectively bypassed, and its write aliases the source
  // register definition.
  struct RegisterRenamingInfo {
    IndexPlusCostPairTy IndexPlusCost;
    MCPhysReg RenameAs;
    MCPhysReg AliasRegID;
    bool AllowMoveElimination;
    RegisterRenamingInfo()
        : IndexPlusCost(std::make_pair(0U, 1U)), RenameAs(0U), AliasRegID(0U),
          AllowMoveElimination(false) {}
  };

  // RegisterMapping objects are mainly used to track physical register
  // definitions and resolve data dependencies.
  //
  // Every register declared by the Target is associated with an instance of
  // RegisterMapping. RegisterMapping objects keep track of writes to a logical
  // register.  That information is used by class RegisterFile to resolve data
  // dependencies, and correctly set latencies for register uses.
  //
  // This implementation does not allow overlapping register files. The only
  // register file that is allowed to overlap with other register files is
  // register file #0. If we exclude register #0, every register is "owned" by
  // at most one register file.
  using RegisterMapping = std::pair<WriteRef, RegisterRenamingInfo>;

  // There is one entry per each register defined by the target.
  std::vector<RegisterMapping> RegisterMappings;

  // Used to track zero registers. There is one bit for each register defined by
  // the target. Bits are set for registers that are known to be zero.
  APInt ZeroRegisters;

  // This method creates a new register file descriptor.
  // The new register file owns all of the registers declared by register
  // classes in the 'RegisterClasses' set.
  //
  // Processor models allow the definition of RegisterFile(s) via tablegen. For
  // example, this is a tablegen definition for a x86 register file for
  // XMM[0-15] and YMM[0-15], that allows up to 60 renames (each rename costs 1
  // physical register).
  //
  //    def FPRegisterFile : RegisterFile<60, [VR128RegClass, VR256RegClass]>
  //
  // Here FPRegisterFile contains all the registers defined by register class
  // VR128RegClass and VR256RegClass. FPRegisterFile implements 60
  // registers which can be used for register renaming purpose.
  void addRegisterFile(const MCRegisterFileDesc &RF,
                       ArrayRef<MCRegisterCostEntry> Entries);

  // Consumes physical registers in each register file specified by the
  // `IndexPlusCostPairTy`. This method is called from `addRegisterMapping()`.
  void allocatePhysRegs(const RegisterRenamingInfo &Entry,
                        MutableArrayRef<unsigned> UsedPhysRegs);

  // Releases previously allocated physical registers from the register file(s).
  // This method is called from `invalidateRegisterMapping()`.
  void freePhysRegs(const RegisterRenamingInfo &Entry,
                    MutableArrayRef<unsigned> FreedPhysRegs);

  // Collects writes that are in a RAW dependency with RS.
  // This method is called from `addRegisterRead()`.
  void collectWrites(const ReadState &RS,
                     SmallVectorImpl<WriteRef> &Writes) const;

  // Create an instance of RegisterMappingTracker for every register file
  // specified by the processor model.
  // If no register file is specified, then this method creates a default
  // register file with an unbounded number of physical registers.
  void initialize(const MCSchedModel &SM, unsigned NumRegs);

public:
  RegisterFile(const MCSchedModel &SM, const MCRegisterInfo &mri,
               unsigned NumRegs = 0);

  // This method updates the register mappings inserting a new register
  // definition. This method is also responsible for updating the number of
  // allocated physical registers in each register file modified by the write.
  // No physical regiser is allocated if this write is from a zero-idiom.
  void addRegisterWrite(WriteRef Write, MutableArrayRef<unsigned> UsedPhysRegs);

  // Collect writes that are in a data dependency with RS, and update RS
  // internal state.
  void addRegisterRead(ReadState &RS, SmallVectorImpl<WriteRef> &Writes) const;

  // Removes write \param WS from the register mappings.
  // Physical registers may be released to reflect this update.
  // No registers are released if this write is from a zero-idiom.
  void removeRegisterWrite(const WriteState &WS,
                           MutableArrayRef<unsigned> FreedPhysRegs);

  // Returns true if a move from RS to WS can be eliminated.
  // On success, it updates WriteState by setting flag `WS.isEliminated`.
  // If RS is a read from a zero register, and WS is eliminated, then
  // `WS.WritesZero` is also set, so that method addRegisterWrite() would not
  // reserve a physical register for it.
  bool tryEliminateMove(WriteState &WS, ReadState &RS);

  // Checks if there are enough physical registers in the register files.
  // Returns a "response mask" where each bit represents the response from a
  // different register file.  A mask of all zeroes means that all register
  // files are available.  Otherwise, the mask can be used to identify which
  // register file was busy.  This sematic allows us to classify dispatch
  // stalls caused by the lack of register file resources.
  //
  // Current implementation can simulate up to 32 register files (including the
  // special register file at index #0).
  unsigned isAvailable(ArrayRef<unsigned> Regs) const;

  // Returns the number of PRFs implemented by this processor.
  unsigned getNumRegisterFiles() const { return RegisterFiles.size(); }

  // Notify each PRF that a new cycle just started.
  void cycleStart();

#ifndef NDEBUG
  void dump() const;
#endif
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_REGISTER_FILE_H
