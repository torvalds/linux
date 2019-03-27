//===- llvm/CodeGen/GlobalISel/RegisterBankInfo.h ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API for the register bank info.
/// This API is responsible for handling the register banks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_REGISTERBANKINFO_H
#define LLVM_CODEGEN_GLOBALISEL_REGISTERBANKINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <initializer_list>
#include <memory>

namespace llvm {

class MachineInstr;
class MachineRegisterInfo;
class raw_ostream;
class RegisterBank;
class TargetInstrInfo;
class TargetRegisterClass;
class TargetRegisterInfo;

/// Holds all the information related to register banks.
class RegisterBankInfo {
public:
  /// Helper struct that represents how a value is partially mapped
  /// into a register.
  /// The StartIdx and Length represent what region of the orginal
  /// value this partial mapping covers.
  /// This can be represented as a Mask of contiguous bit starting
  /// at StartIdx bit and spanning Length bits.
  /// StartIdx is the number of bits from the less significant bits.
  struct PartialMapping {
    /// Number of bits at which this partial mapping starts in the
    /// original value.  The bits are counted from less significant
    /// bits to most significant bits.
    unsigned StartIdx;

    /// Length of this mapping in bits. This is how many bits this
    /// partial mapping covers in the original value:
    /// from StartIdx to StartIdx + Length -1.
    unsigned Length;

    /// Register bank where the partial value lives.
    const RegisterBank *RegBank;

    PartialMapping() = default;

    /// Provide a shortcut for quickly building PartialMapping.
    PartialMapping(unsigned StartIdx, unsigned Length,
                   const RegisterBank &RegBank)
        : StartIdx(StartIdx), Length(Length), RegBank(&RegBank) {}

    /// \return the index of in the original value of the most
    /// significant bit that this partial mapping covers.
    unsigned getHighBitIdx() const { return StartIdx + Length - 1; }

    /// Print this partial mapping on dbgs() stream.
    void dump() const;

    /// Print this partial mapping on \p OS;
    void print(raw_ostream &OS) const;

    /// Check that the Mask is compatible with the RegBank.
    /// Indeed, if the RegBank cannot accomadate the "active bits" of the mask,
    /// there is no way this mapping is valid.
    ///
    /// \note This method does not check anything when assertions are disabled.
    ///
    /// \return True is the check was successful.
    bool verify() const;
  };

  /// Helper struct that represents how a value is mapped through
  /// different register banks.
  ///
  /// \note: So far we do not have any users of the complex mappings
  /// (mappings with more than one partial mapping), but when we do,
  /// we would have needed to duplicate partial mappings.
  /// The alternative could be to use an array of pointers of partial
  /// mapping (i.e., PartialMapping **BreakDown) and duplicate the
  /// pointers instead.
  ///
  /// E.g.,
  /// Let say we have a 32-bit add and a <2 x 32-bit> vadd. We
  /// can expand the
  /// <2 x 32-bit> add into 2 x 32-bit add.
  ///
  /// Currently the TableGen-like file would look like:
  /// \code
  /// PartialMapping[] = {
  /// /*32-bit add*/    {0, 32, GPR}, // Scalar entry repeated for first vec elt.
  /// /*2x32-bit add*/  {0, 32, GPR}, {32, 32, GPR},
  /// /*<2x32-bit> vadd {0, 64, VPR}
  /// }; // PartialMapping duplicated.
  ///
  /// ValueMapping[] {
  ///   /*plain 32-bit add*/ {&PartialMapping[0], 1},
  ///   /*expanded vadd on 2xadd*/ {&PartialMapping[1], 2},
  ///   /*plain <2x32-bit> vadd*/ {&PartialMapping[3], 1}
  /// };
  /// \endcode
  ///
  /// With the array of pointer, we would have:
  /// \code
  /// PartialMapping[] = {
  /// /*32-bit add lower */ {0, 32, GPR},
  /// /*32-bit add upper */ {32, 32, GPR},
  /// /*<2x32-bit> vadd {0, 64, VPR}
  /// }; // No more duplication.
  ///
  /// BreakDowns[] = {
  /// /*AddBreakDown*/ &PartialMapping[0],
  /// /*2xAddBreakDown*/ &PartialMapping[0], &PartialMapping[1],
  /// /*VAddBreakDown*/ &PartialMapping[2]
  /// }; // Addresses of PartialMapping duplicated (smaller).
  ///
  /// ValueMapping[] {
  ///   /*plain 32-bit add*/ {&BreakDowns[0], 1},
  ///   /*expanded vadd on 2xadd*/ {&BreakDowns[1], 2},
  ///   /*plain <2x32-bit> vadd*/ {&BreakDowns[3], 1}
  /// };
  /// \endcode
  ///
  /// Given that a PartialMapping is actually small, the code size
  /// impact is actually a degradation. Moreover the compile time will
  /// be hit by the additional indirection.
  /// If PartialMapping gets bigger we may reconsider.
  struct ValueMapping {
    /// How the value is broken down between the different register banks.
    const PartialMapping *BreakDown;

    /// Number of partial mapping to break down this value.
    unsigned NumBreakDowns;

    /// The default constructor creates an invalid (isValid() == false)
    /// instance.
    ValueMapping() : ValueMapping(nullptr, 0) {}

    /// Initialize a ValueMapping with the given parameter.
    /// \p BreakDown needs to have a life time at least as long
    /// as this instance.
    ValueMapping(const PartialMapping *BreakDown, unsigned NumBreakDowns)
        : BreakDown(BreakDown), NumBreakDowns(NumBreakDowns) {}

    /// Iterators through the PartialMappings.
    const PartialMapping *begin() const { return BreakDown; }
    const PartialMapping *end() const { return BreakDown + NumBreakDowns; }

    /// Check if this ValueMapping is valid.
    bool isValid() const { return BreakDown && NumBreakDowns; }

    /// Verify that this mapping makes sense for a value of
    /// \p MeaningfulBitWidth.
    /// \note This method does not check anything when assertions are disabled.
    ///
    /// \return True is the check was successful.
    bool verify(unsigned MeaningfulBitWidth) const;

    /// Print this on dbgs() stream.
    void dump() const;

    /// Print this on \p OS;
    void print(raw_ostream &OS) const;
  };

  /// Helper class that represents how the value of an instruction may be
  /// mapped and what is the related cost of such mapping.
  class InstructionMapping {
    /// Identifier of the mapping.
    /// This is used to communicate between the target and the optimizers
    /// which mapping should be realized.
    unsigned ID = InvalidMappingID;

    /// Cost of this mapping.
    unsigned Cost = 0;

    /// Mapping of all the operands.
    const ValueMapping *OperandsMapping;

    /// Number of operands.
    unsigned NumOperands = 0;

    const ValueMapping &getOperandMapping(unsigned i) {
      assert(i < getNumOperands() && "Out of bound operand");
      return OperandsMapping[i];
    }

  public:
    /// Constructor for the mapping of an instruction.
    /// \p NumOperands must be equal to number of all the operands of
    /// the related instruction.
    /// The rationale is that it is more efficient for the optimizers
    /// to be able to assume that the mapping of the ith operand is
    /// at the index i.
    ///
    /// \pre ID != InvalidMappingID
    InstructionMapping(unsigned ID, unsigned Cost,
                       const ValueMapping *OperandsMapping,
                       unsigned NumOperands)
        : ID(ID), Cost(Cost), OperandsMapping(OperandsMapping),
          NumOperands(NumOperands) {
      assert(getID() != InvalidMappingID &&
             "Use the default constructor for invalid mapping");
    }

    /// Default constructor.
    /// Use this constructor to express that the mapping is invalid.
    InstructionMapping() = default;

    /// Get the cost.
    unsigned getCost() const { return Cost; }

    /// Get the ID.
    unsigned getID() const { return ID; }

    /// Get the number of operands.
    unsigned getNumOperands() const { return NumOperands; }

    /// Get the value mapping of the ith operand.
    /// \pre The mapping for the ith operand has been set.
    /// \pre The ith operand is a register.
    const ValueMapping &getOperandMapping(unsigned i) const {
      const ValueMapping &ValMapping =
          const_cast<InstructionMapping *>(this)->getOperandMapping(i);
      return ValMapping;
    }

    /// Set the mapping for all the operands.
    /// In other words, OpdsMapping should hold at least getNumOperands
    /// ValueMapping.
    void setOperandsMapping(const ValueMapping *OpdsMapping) {
      OperandsMapping = OpdsMapping;
    }

    /// Check whether this object is valid.
    /// This is a lightweight check for obvious wrong instance.
    bool isValid() const {
      return getID() != InvalidMappingID && OperandsMapping;
    }

    /// Verifiy that this mapping makes sense for \p MI.
    /// \pre \p MI must be connected to a MachineFunction.
    ///
    /// \note This method does not check anything when assertions are disabled.
    ///
    /// \return True is the check was successful.
    bool verify(const MachineInstr &MI) const;

    /// Print this on dbgs() stream.
    void dump() const;

    /// Print this on \p OS;
    void print(raw_ostream &OS) const;
  };

  /// Convenient type to represent the alternatives for mapping an
  /// instruction.
  /// \todo When we move to TableGen this should be an array ref.
  using InstructionMappings = SmallVector<const InstructionMapping *, 4>;

  /// Helper class used to get/create the virtual registers that will be used
  /// to replace the MachineOperand when applying a mapping.
  class OperandsMapper {
    /// The OpIdx-th cell contains the index in NewVRegs where the VRegs of the
    /// OpIdx-th operand starts. -1 means we do not have such mapping yet.
    /// Note: We use a SmallVector to avoid heap allocation for most cases.
    SmallVector<int, 8> OpToNewVRegIdx;

    /// Hold the registers that will be used to map MI with InstrMapping.
    SmallVector<unsigned, 8> NewVRegs;

    /// Current MachineRegisterInfo, used to create new virtual registers.
    MachineRegisterInfo &MRI;

    /// Instruction being remapped.
    MachineInstr &MI;

    /// New mapping of the instruction.
    const InstructionMapping &InstrMapping;

    /// Constant value identifying that the index in OpToNewVRegIdx
    /// for an operand has not been set yet.
    static const int DontKnowIdx;

    /// Get the range in NewVRegs to store all the partial
    /// values for the \p OpIdx-th operand.
    ///
    /// \return The iterator range for the space created.
    //
    /// \pre getMI().getOperand(OpIdx).isReg()
    iterator_range<SmallVectorImpl<unsigned>::iterator>
    getVRegsMem(unsigned OpIdx);

    /// Get the end iterator for a range starting at \p StartIdx and
    /// spannig \p NumVal in NewVRegs.
    /// \pre StartIdx + NumVal <= NewVRegs.size()
    SmallVectorImpl<unsigned>::const_iterator
    getNewVRegsEnd(unsigned StartIdx, unsigned NumVal) const;
    SmallVectorImpl<unsigned>::iterator getNewVRegsEnd(unsigned StartIdx,
                                                       unsigned NumVal);

  public:
    /// Create an OperandsMapper that will hold the information to apply \p
    /// InstrMapping to \p MI.
    /// \pre InstrMapping.verify(MI)
    OperandsMapper(MachineInstr &MI, const InstructionMapping &InstrMapping,
                   MachineRegisterInfo &MRI);

    /// \name Getters.
    /// @{
    /// The MachineInstr being remapped.
    MachineInstr &getMI() const { return MI; }

    /// The final mapping of the instruction.
    const InstructionMapping &getInstrMapping() const { return InstrMapping; }

    /// The MachineRegisterInfo we used to realize the mapping.
    MachineRegisterInfo &getMRI() const { return MRI; }
    /// @}

    /// Create as many new virtual registers as needed for the mapping of the \p
    /// OpIdx-th operand.
    /// The number of registers is determined by the number of breakdown for the
    /// related operand in the instruction mapping.
    /// The type of the new registers is a plain scalar of the right size.
    /// The proper type is expected to be set when the mapping is applied to
    /// the instruction(s) that realizes the mapping.
    ///
    /// \pre getMI().getOperand(OpIdx).isReg()
    ///
    /// \post All the partial mapping of the \p OpIdx-th operand have been
    /// assigned a new virtual register.
    void createVRegs(unsigned OpIdx);

    /// Set the virtual register of the \p PartialMapIdx-th partial mapping of
    /// the OpIdx-th operand to \p NewVReg.
    ///
    /// \pre getMI().getOperand(OpIdx).isReg()
    /// \pre getInstrMapping().getOperandMapping(OpIdx).BreakDown.size() >
    /// PartialMapIdx
    /// \pre NewReg != 0
    ///
    /// \post the \p PartialMapIdx-th register of the value mapping of the \p
    /// OpIdx-th operand has been set.
    void setVRegs(unsigned OpIdx, unsigned PartialMapIdx, unsigned NewVReg);

    /// Get all the virtual registers required to map the \p OpIdx-th operand of
    /// the instruction.
    ///
    /// This return an empty range when createVRegs or setVRegs has not been
    /// called.
    /// The iterator may be invalidated by a call to setVRegs or createVRegs.
    ///
    /// When \p ForDebug is true, we will not check that the list of new virtual
    /// registers does not contain uninitialized values.
    ///
    /// \pre getMI().getOperand(OpIdx).isReg()
    /// \pre ForDebug || All partial mappings have been set a register
    iterator_range<SmallVectorImpl<unsigned>::const_iterator>
    getVRegs(unsigned OpIdx, bool ForDebug = false) const;

    /// Print this operands mapper on dbgs() stream.
    void dump() const;

    /// Print this operands mapper on \p OS stream.
    void print(raw_ostream &OS, bool ForDebug = false) const;
  };

protected:
  /// Hold the set of supported register banks.
  RegisterBank **RegBanks;

  /// Total number of register banks.
  unsigned NumRegBanks;

  /// Keep dynamically allocated PartialMapping in a separate map.
  /// This shouldn't be needed when everything gets TableGen'ed.
  mutable DenseMap<unsigned, std::unique_ptr<const PartialMapping>>
      MapOfPartialMappings;

  /// Keep dynamically allocated ValueMapping in a separate map.
  /// This shouldn't be needed when everything gets TableGen'ed.
  mutable DenseMap<unsigned, std::unique_ptr<const ValueMapping>>
      MapOfValueMappings;

  /// Keep dynamically allocated array of ValueMapping in a separate map.
  /// This shouldn't be needed when everything gets TableGen'ed.
  mutable DenseMap<unsigned, std::unique_ptr<ValueMapping[]>>
      MapOfOperandsMappings;

  /// Keep dynamically allocated InstructionMapping in a separate map.
  /// This shouldn't be needed when everything gets TableGen'ed.
  mutable DenseMap<unsigned, std::unique_ptr<const InstructionMapping>>
      MapOfInstructionMappings;

  /// Getting the minimal register class of a physreg is expensive.
  /// Cache this information as we get it.
  mutable DenseMap<unsigned, const TargetRegisterClass *> PhysRegMinimalRCs;

  /// Create a RegisterBankInfo that can accommodate up to \p NumRegBanks
  /// RegisterBank instances.
  RegisterBankInfo(RegisterBank **RegBanks, unsigned NumRegBanks);

  /// This constructor is meaningless.
  /// It just provides a default constructor that can be used at link time
  /// when GlobalISel is not built.
  /// That way, targets can still inherit from this class without doing
  /// crazy gymnastic to avoid link time failures.
  /// \note That works because the constructor is inlined.
  RegisterBankInfo() {
    llvm_unreachable("This constructor should not be executed");
  }

  /// Get the register bank identified by \p ID.
  RegisterBank &getRegBank(unsigned ID) {
    assert(ID < getNumRegBanks() && "Accessing an unknown register bank");
    return *RegBanks[ID];
  }

  /// Get the MinimalPhysRegClass for Reg.
  /// \pre Reg is a physical register.
  const TargetRegisterClass &
  getMinimalPhysRegClass(unsigned Reg, const TargetRegisterInfo &TRI) const;

  /// Try to get the mapping of \p MI.
  /// See getInstrMapping for more details on what a mapping represents.
  ///
  /// Unlike getInstrMapping the returned InstructionMapping may be invalid
  /// (isValid() == false).
  /// This means that the target independent code is not smart enough
  /// to get the mapping of \p MI and thus, the target has to provide the
  /// information for \p MI.
  ///
  /// This implementation is able to get the mapping of:
  /// - Target specific instructions by looking at the encoding constraints.
  /// - Any instruction if all the register operands have already been assigned
  ///   a register, a register class, or a register bank.
  /// - Copies and phis if at least one of the operands has been assigned a
  ///   register, a register class, or a register bank.
  /// In other words, this method will likely fail to find a mapping for
  /// any generic opcode that has not been lowered by target specific code.
  const InstructionMapping &getInstrMappingImpl(const MachineInstr &MI) const;

  /// Get the uniquely generated PartialMapping for the
  /// given arguments.
  const PartialMapping &getPartialMapping(unsigned StartIdx, unsigned Length,
                                          const RegisterBank &RegBank) const;

  /// \name Methods to get a uniquely generated ValueMapping.
  /// @{

  /// The most common ValueMapping consists of a single PartialMapping.
  /// Feature a method for that.
  const ValueMapping &getValueMapping(unsigned StartIdx, unsigned Length,
                                      const RegisterBank &RegBank) const;

  /// Get the ValueMapping for the given arguments.
  const ValueMapping &getValueMapping(const PartialMapping *BreakDown,
                                      unsigned NumBreakDowns) const;
  /// @}

  /// \name Methods to get a uniquely generated array of ValueMapping.
  /// @{

  /// Get the uniquely generated array of ValueMapping for the
  /// elements of between \p Begin and \p End.
  ///
  /// Elements that are nullptr will be replaced by
  /// invalid ValueMapping (ValueMapping::isValid == false).
  ///
  /// \pre The pointers on ValueMapping between \p Begin and \p End
  /// must uniquely identify a ValueMapping. Otherwise, there is no
  /// guarantee that the return instance will be unique, i.e., another
  /// OperandsMapping could have the same content.
  template <typename Iterator>
  const ValueMapping *getOperandsMapping(Iterator Begin, Iterator End) const;

  /// Get the uniquely generated array of ValueMapping for the
  /// elements of \p OpdsMapping.
  ///
  /// Elements of \p OpdsMapping that are nullptr will be replaced by
  /// invalid ValueMapping (ValueMapping::isValid == false).
  const ValueMapping *getOperandsMapping(
      const SmallVectorImpl<const ValueMapping *> &OpdsMapping) const;

  /// Get the uniquely generated array of ValueMapping for the
  /// given arguments.
  ///
  /// Arguments that are nullptr will be replaced by invalid
  /// ValueMapping (ValueMapping::isValid == false).
  const ValueMapping *getOperandsMapping(
      std::initializer_list<const ValueMapping *> OpdsMapping) const;
  /// @}

  /// \name Methods to get a uniquely generated InstructionMapping.
  /// @{

private:
  /// Method to get a uniquely generated InstructionMapping.
  const InstructionMapping &
  getInstructionMappingImpl(bool IsInvalid, unsigned ID = InvalidMappingID,
                            unsigned Cost = 0,
                            const ValueMapping *OperandsMapping = nullptr,
                            unsigned NumOperands = 0) const;

public:
  /// Method to get a uniquely generated InstructionMapping.
  const InstructionMapping &
  getInstructionMapping(unsigned ID, unsigned Cost,
                        const ValueMapping *OperandsMapping,
                        unsigned NumOperands) const {
    return getInstructionMappingImpl(/*IsInvalid*/ false, ID, Cost,
                                     OperandsMapping, NumOperands);
  }

  /// Method to get a uniquely generated invalid InstructionMapping.
  const InstructionMapping &getInvalidInstructionMapping() const {
    return getInstructionMappingImpl(/*IsInvalid*/ true);
  }
  /// @}

  /// Get the register bank for the \p OpIdx-th operand of \p MI form
  /// the encoding constraints, if any.
  ///
  /// \return A register bank that covers the register class of the
  /// related encoding constraints or nullptr if \p MI did not provide
  /// enough information to deduce it.
  const RegisterBank *
  getRegBankFromConstraints(const MachineInstr &MI, unsigned OpIdx,
                            const TargetInstrInfo &TII,
                            const TargetRegisterInfo &TRI) const;

  /// Helper method to apply something that is like the default mapping.
  /// Basically, that means that \p OpdMapper.getMI() is left untouched
  /// aside from the reassignment of the register operand that have been
  /// remapped.
  ///
  /// The type of all the new registers that have been created by the
  /// mapper are properly remapped to the type of the original registers
  /// they replace. In other words, the semantic of the instruction does
  /// not change, only the register banks.
  ///
  /// If the mapping of one of the operand spans several registers, this
  /// method will abort as this is not like a default mapping anymore.
  ///
  /// \pre For OpIdx in {0..\p OpdMapper.getMI().getNumOperands())
  ///        the range OpdMapper.getVRegs(OpIdx) is empty or of size 1.
  static void applyDefaultMapping(const OperandsMapper &OpdMapper);

  /// See ::applyMapping.
  virtual void applyMappingImpl(const OperandsMapper &OpdMapper) const {
    llvm_unreachable("The target has to implement that part");
  }

public:
  virtual ~RegisterBankInfo() = default;

  /// Get the register bank identified by \p ID.
  const RegisterBank &getRegBank(unsigned ID) const {
    return const_cast<RegisterBankInfo *>(this)->getRegBank(ID);
  }

  /// Get the register bank of \p Reg.
  /// If Reg has not been assigned a register, a register class,
  /// or a register bank, then this returns nullptr.
  ///
  /// \pre Reg != 0 (NoRegister)
  const RegisterBank *getRegBank(unsigned Reg, const MachineRegisterInfo &MRI,
                                 const TargetRegisterInfo &TRI) const;

  /// Get the total number of register banks.
  unsigned getNumRegBanks() const { return NumRegBanks; }

  /// Get a register bank that covers \p RC.
  ///
  /// \pre \p RC is a user-defined register class (as opposed as one
  /// generated by TableGen).
  ///
  /// \note The mapping RC -> RegBank could be built while adding the
  /// coverage for the register banks. However, we do not do it, because,
  /// at least for now, we only need this information for register classes
  /// that are used in the description of instruction. In other words,
  /// there are just a handful of them and we do not want to waste space.
  ///
  /// \todo This should be TableGen'ed.
  virtual const RegisterBank &
  getRegBankFromRegClass(const TargetRegisterClass &RC) const {
    llvm_unreachable("The target must override this method");
  }

  /// Get the cost of a copy from \p B to \p A, or put differently,
  /// get the cost of A = COPY B. Since register banks may cover
  /// different size, \p Size specifies what will be the size in bits
  /// that will be copied around.
  ///
  /// \note Since this is a copy, both registers have the same size.
  virtual unsigned copyCost(const RegisterBank &A, const RegisterBank &B,
                            unsigned Size) const {
    // Optimistically assume that copies are coalesced. I.e., when
    // they are on the same bank, they are free.
    // Otherwise assume a non-zero cost of 1. The targets are supposed
    // to override that properly anyway if they care.
    return &A != &B;
  }

  /// Constrain the (possibly generic) virtual register \p Reg to \p RC.
  ///
  /// \pre \p Reg is a virtual register that either has a bank or a class.
  /// \returns The constrained register class, or nullptr if there is none.
  /// \note This is a generic variant of MachineRegisterInfo::constrainRegClass
  /// \note Use MachineRegisterInfo::constrainRegAttrs instead for any non-isel
  /// purpose, including non-select passes of GlobalISel
  static const TargetRegisterClass *
  constrainGenericRegister(unsigned Reg, const TargetRegisterClass &RC,
                           MachineRegisterInfo &MRI);

  /// Identifier used when the related instruction mapping instance
  /// is generated by target independent code.
  /// Make sure not to use that identifier to avoid possible collision.
  static const unsigned DefaultMappingID;

  /// Identifier used when the related instruction mapping instance
  /// is generated by the default constructor.
  /// Make sure not to use that identifier.
  static const unsigned InvalidMappingID;

  /// Get the mapping of the different operands of \p MI
  /// on the register bank.
  /// This mapping should be the direct translation of \p MI.
  /// In other words, when \p MI is mapped with the returned mapping,
  /// only the register banks of the operands of \p MI need to be updated.
  /// In particular, neither the opcode nor the type of \p MI needs to be
  /// updated for this direct mapping.
  ///
  /// The target independent implementation gives a mapping based on
  /// the register classes for the target specific opcode.
  /// It uses the ID RegisterBankInfo::DefaultMappingID for that mapping.
  /// Make sure you do not use that ID for the alternative mapping
  /// for MI. See getInstrAlternativeMappings for the alternative
  /// mappings.
  ///
  /// For instance, if \p MI is a vector add, the mapping should
  /// not be a scalarization of the add.
  ///
  /// \post returnedVal.verify(MI).
  ///
  /// \note If returnedVal does not verify MI, this would probably mean
  /// that the target does not support that instruction.
  virtual const InstructionMapping &
  getInstrMapping(const MachineInstr &MI) const;

  /// Get the alternative mappings for \p MI.
  /// Alternative in the sense different from getInstrMapping.
  virtual InstructionMappings
  getInstrAlternativeMappings(const MachineInstr &MI) const;

  /// Get the possible mapping for \p MI.
  /// A mapping defines where the different operands may live and at what cost.
  /// For instance, let us consider:
  /// v0(16) = G_ADD <2 x i8> v1, v2
  /// The possible mapping could be:
  ///
  /// {/*ID*/VectorAdd, /*Cost*/1, /*v0*/{(0xFFFF, VPR)}, /*v1*/{(0xFFFF, VPR)},
  ///                              /*v2*/{(0xFFFF, VPR)}}
  /// {/*ID*/ScalarAddx2, /*Cost*/2, /*v0*/{(0x00FF, GPR),(0xFF00, GPR)},
  ///                                /*v1*/{(0x00FF, GPR),(0xFF00, GPR)},
  ///                                /*v2*/{(0x00FF, GPR),(0xFF00, GPR)}}
  ///
  /// \note The first alternative of the returned mapping should be the
  /// direct translation of \p MI current form.
  ///
  /// \post !returnedVal.empty().
  InstructionMappings getInstrPossibleMappings(const MachineInstr &MI) const;

  /// Apply \p OpdMapper.getInstrMapping() to \p OpdMapper.getMI().
  /// After this call \p OpdMapper.getMI() may not be valid anymore.
  /// \p OpdMapper.getInstrMapping().getID() carries the information of
  /// what has been chosen to map \p OpdMapper.getMI(). This ID is set
  /// by the various getInstrXXXMapping method.
  ///
  /// Therefore, getting the mapping and applying it should be kept in
  /// sync.
  void applyMapping(const OperandsMapper &OpdMapper) const {
    // The only mapping we know how to handle is the default mapping.
    if (OpdMapper.getInstrMapping().getID() == DefaultMappingID)
      return applyDefaultMapping(OpdMapper);
    // For other mapping, the target needs to do the right thing.
    // If that means calling applyDefaultMapping, fine, but this
    // must be explicitly stated.
    applyMappingImpl(OpdMapper);
  }

  /// Get the size in bits of \p Reg.
  /// Utility method to get the size of any registers. Unlike
  /// MachineRegisterInfo::getSize, the register does not need to be a
  /// virtual register.
  ///
  /// \pre \p Reg != 0 (NoRegister).
  unsigned getSizeInBits(unsigned Reg, const MachineRegisterInfo &MRI,
                         const TargetRegisterInfo &TRI) const;

  /// Check that information hold by this instance make sense for the
  /// given \p TRI.
  ///
  /// \note This method does not check anything when assertions are disabled.
  ///
  /// \return True is the check was successful.
  bool verify(const TargetRegisterInfo &TRI) const;
};

inline raw_ostream &
operator<<(raw_ostream &OS,
           const RegisterBankInfo::PartialMapping &PartMapping) {
  PartMapping.print(OS);
  return OS;
}

inline raw_ostream &
operator<<(raw_ostream &OS, const RegisterBankInfo::ValueMapping &ValMapping) {
  ValMapping.print(OS);
  return OS;
}

inline raw_ostream &
operator<<(raw_ostream &OS,
           const RegisterBankInfo::InstructionMapping &InstrMapping) {
  InstrMapping.print(OS);
  return OS;
}

inline raw_ostream &
operator<<(raw_ostream &OS, const RegisterBankInfo::OperandsMapper &OpdMapper) {
  OpdMapper.print(OS, /*ForDebug*/ false);
  return OS;
}

/// Hashing function for PartialMapping.
/// It is required for the hashing of ValueMapping.
hash_code hash_value(const RegisterBankInfo::PartialMapping &PartMapping);

} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_REGISTERBANKINFO_H
