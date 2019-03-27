//===- llvm/CodeGen/MachineRegisterInfo.h -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachineRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEREGISTERINFO_H
#define LLVM_CODEGEN_MACHINEREGISTERINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/GlobalISel/RegisterBank.h"
#include "llvm/CodeGen/LowLevelType.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class PSetIterator;

/// Convenient type to represent either a register class or a register bank.
using RegClassOrRegBank =
    PointerUnion<const TargetRegisterClass *, const RegisterBank *>;

/// MachineRegisterInfo - Keep track of information for virtual and physical
/// registers, including vreg register classes, use/def chains for registers,
/// etc.
class MachineRegisterInfo {
public:
  class Delegate {
    virtual void anchor();

  public:
    virtual ~Delegate() = default;

    virtual void MRI_NoteNewVirtualRegister(unsigned Reg) = 0;
  };

private:
  MachineFunction *MF;
  Delegate *TheDelegate = nullptr;

  /// True if subregister liveness is tracked.
  const bool TracksSubRegLiveness;

  /// VRegInfo - Information we keep for each virtual register.
  ///
  /// Each element in this list contains the register class of the vreg and the
  /// start of the use/def list for the register.
  IndexedMap<std::pair<RegClassOrRegBank, MachineOperand *>,
             VirtReg2IndexFunctor>
      VRegInfo;

  /// Map for recovering vreg name from vreg number.
  /// This map is used by the MIR Printer.
  IndexedMap<std::string, VirtReg2IndexFunctor> VReg2Name;

  /// StringSet that is used to unique vreg names.
  StringSet<> VRegNames;

  /// The flag is true upon \p UpdatedCSRs initialization
  /// and false otherwise.
  bool IsUpdatedCSRsInitialized;

  /// Contains the updated callee saved register list.
  /// As opposed to the static list defined in register info,
  /// all registers that were disabled are removed from the list.
  SmallVector<MCPhysReg, 16> UpdatedCSRs;

  /// RegAllocHints - This vector records register allocation hints for
  /// virtual registers. For each virtual register, it keeps a pair of hint
  /// type and hints vector making up the allocation hints. Only the first
  /// hint may be target specific, and in that case this is reflected by the
  /// first member of the pair being non-zero. If the hinted register is
  /// virtual, it means the allocator should prefer the physical register
  /// allocated to it if any.
  IndexedMap<std::pair<unsigned, SmallVector<unsigned, 4>>,
             VirtReg2IndexFunctor> RegAllocHints;

  /// PhysRegUseDefLists - This is an array of the head of the use/def list for
  /// physical registers.
  std::unique_ptr<MachineOperand *[]> PhysRegUseDefLists;

  /// getRegUseDefListHead - Return the head pointer for the register use/def
  /// list for the specified virtual or physical register.
  MachineOperand *&getRegUseDefListHead(unsigned RegNo) {
    if (TargetRegisterInfo::isVirtualRegister(RegNo))
      return VRegInfo[RegNo].second;
    return PhysRegUseDefLists[RegNo];
  }

  MachineOperand *getRegUseDefListHead(unsigned RegNo) const {
    if (TargetRegisterInfo::isVirtualRegister(RegNo))
      return VRegInfo[RegNo].second;
    return PhysRegUseDefLists[RegNo];
  }

  /// Get the next element in the use-def chain.
  static MachineOperand *getNextOperandForReg(const MachineOperand *MO) {
    assert(MO && MO->isReg() && "This is not a register operand!");
    return MO->Contents.Reg.Next;
  }

  /// UsedPhysRegMask - Additional used physregs including aliases.
  /// This bit vector represents all the registers clobbered by function calls.
  BitVector UsedPhysRegMask;

  /// ReservedRegs - This is a bit vector of reserved registers.  The target
  /// may change its mind about which registers should be reserved.  This
  /// vector is the frozen set of reserved registers when register allocation
  /// started.
  BitVector ReservedRegs;

  using VRegToTypeMap = IndexedMap<LLT, VirtReg2IndexFunctor>;
  /// Map generic virtual registers to their low-level type.
  VRegToTypeMap VRegToType;

  /// Keep track of the physical registers that are live in to the function.
  /// Live in values are typically arguments in registers.  LiveIn values are
  /// allowed to have virtual registers associated with them, stored in the
  /// second element.
  std::vector<std::pair<unsigned, unsigned>> LiveIns;

public:
  explicit MachineRegisterInfo(MachineFunction *MF);
  MachineRegisterInfo(const MachineRegisterInfo &) = delete;
  MachineRegisterInfo &operator=(const MachineRegisterInfo &) = delete;

  const TargetRegisterInfo *getTargetRegisterInfo() const {
    return MF->getSubtarget().getRegisterInfo();
  }

  void resetDelegate(Delegate *delegate) {
    // Ensure another delegate does not take over unless the current
    // delegate first unattaches itself. If we ever need to multicast
    // notifications, we will need to change to using a list.
    assert(TheDelegate == delegate &&
           "Only the current delegate can perform reset!");
    TheDelegate = nullptr;
  }

  void setDelegate(Delegate *delegate) {
    assert(delegate && !TheDelegate &&
           "Attempted to set delegate to null, or to change it without "
           "first resetting it!");

    TheDelegate = delegate;
  }

  //===--------------------------------------------------------------------===//
  // Function State
  //===--------------------------------------------------------------------===//

  // isSSA - Returns true when the machine function is in SSA form. Early
  // passes require the machine function to be in SSA form where every virtual
  // register has a single defining instruction.
  //
  // The TwoAddressInstructionPass and PHIElimination passes take the machine
  // function out of SSA form when they introduce multiple defs per virtual
  // register.
  bool isSSA() const {
    return MF->getProperties().hasProperty(
        MachineFunctionProperties::Property::IsSSA);
  }

  // leaveSSA - Indicates that the machine function is no longer in SSA form.
  void leaveSSA() {
    MF->getProperties().reset(MachineFunctionProperties::Property::IsSSA);
  }

  /// tracksLiveness - Returns true when tracking register liveness accurately.
  /// (see MachineFUnctionProperties::Property description for details)
  bool tracksLiveness() const {
    return MF->getProperties().hasProperty(
        MachineFunctionProperties::Property::TracksLiveness);
  }

  /// invalidateLiveness - Indicates that register liveness is no longer being
  /// tracked accurately.
  ///
  /// This should be called by late passes that invalidate the liveness
  /// information.
  void invalidateLiveness() {
    MF->getProperties().reset(
        MachineFunctionProperties::Property::TracksLiveness);
  }

  /// Returns true if liveness for register class @p RC should be tracked at
  /// the subregister level.
  bool shouldTrackSubRegLiveness(const TargetRegisterClass &RC) const {
    return subRegLivenessEnabled() && RC.HasDisjunctSubRegs;
  }
  bool shouldTrackSubRegLiveness(unsigned VReg) const {
    assert(TargetRegisterInfo::isVirtualRegister(VReg) && "Must pass a VReg");
    return shouldTrackSubRegLiveness(*getRegClass(VReg));
  }
  bool subRegLivenessEnabled() const {
    return TracksSubRegLiveness;
  }

  //===--------------------------------------------------------------------===//
  // Register Info
  //===--------------------------------------------------------------------===//

  /// Returns true if the updated CSR list was initialized and false otherwise.
  bool isUpdatedCSRsInitialized() const { return IsUpdatedCSRsInitialized; }

  /// Disables the register from the list of CSRs.
  /// I.e. the register will not appear as part of the CSR mask.
  /// \see UpdatedCalleeSavedRegs.
  void disableCalleeSavedRegister(unsigned Reg);

  /// Returns list of callee saved registers.
  /// The function returns the updated CSR list (after taking into account
  /// registers that are disabled from the CSR list).
  const MCPhysReg *getCalleeSavedRegs() const;

  /// Sets the updated Callee Saved Registers list.
  /// Notice that it will override ant previously disabled/saved CSRs.
  void setCalleeSavedRegs(ArrayRef<MCPhysReg> CSRs);

  // Strictly for use by MachineInstr.cpp.
  void addRegOperandToUseList(MachineOperand *MO);

  // Strictly for use by MachineInstr.cpp.
  void removeRegOperandFromUseList(MachineOperand *MO);

  // Strictly for use by MachineInstr.cpp.
  void moveOperands(MachineOperand *Dst, MachineOperand *Src, unsigned NumOps);

  /// Verify the sanity of the use list for Reg.
  void verifyUseList(unsigned Reg) const;

  /// Verify the use list of all registers.
  void verifyUseLists() const;

  /// reg_begin/reg_end - Provide iteration support to walk over all definitions
  /// and uses of a register within the MachineFunction that corresponds to this
  /// MachineRegisterInfo object.
  template<bool Uses, bool Defs, bool SkipDebug,
           bool ByOperand, bool ByInstr, bool ByBundle>
  class defusechain_iterator;
  template<bool Uses, bool Defs, bool SkipDebug,
           bool ByOperand, bool ByInstr, bool ByBundle>
  class defusechain_instr_iterator;

  // Make it a friend so it can access getNextOperandForReg().
  template<bool, bool, bool, bool, bool, bool>
    friend class defusechain_iterator;
  template<bool, bool, bool, bool, bool, bool>
    friend class defusechain_instr_iterator;

  /// reg_iterator/reg_begin/reg_end - Walk all defs and uses of the specified
  /// register.
  using reg_iterator =
      defusechain_iterator<true, true, false, true, false, false>;
  reg_iterator reg_begin(unsigned RegNo) const {
    return reg_iterator(getRegUseDefListHead(RegNo));
  }
  static reg_iterator reg_end() { return reg_iterator(nullptr); }

  inline iterator_range<reg_iterator>  reg_operands(unsigned Reg) const {
    return make_range(reg_begin(Reg), reg_end());
  }

  /// reg_instr_iterator/reg_instr_begin/reg_instr_end - Walk all defs and uses
  /// of the specified register, stepping by MachineInstr.
  using reg_instr_iterator =
      defusechain_instr_iterator<true, true, false, false, true, false>;
  reg_instr_iterator reg_instr_begin(unsigned RegNo) const {
    return reg_instr_iterator(getRegUseDefListHead(RegNo));
  }
  static reg_instr_iterator reg_instr_end() {
    return reg_instr_iterator(nullptr);
  }

  inline iterator_range<reg_instr_iterator>
  reg_instructions(unsigned Reg) const {
    return make_range(reg_instr_begin(Reg), reg_instr_end());
  }

  /// reg_bundle_iterator/reg_bundle_begin/reg_bundle_end - Walk all defs and uses
  /// of the specified register, stepping by bundle.
  using reg_bundle_iterator =
      defusechain_instr_iterator<true, true, false, false, false, true>;
  reg_bundle_iterator reg_bundle_begin(unsigned RegNo) const {
    return reg_bundle_iterator(getRegUseDefListHead(RegNo));
  }
  static reg_bundle_iterator reg_bundle_end() {
    return reg_bundle_iterator(nullptr);
  }

  inline iterator_range<reg_bundle_iterator> reg_bundles(unsigned Reg) const {
    return make_range(reg_bundle_begin(Reg), reg_bundle_end());
  }

  /// reg_empty - Return true if there are no instructions using or defining the
  /// specified register (it may be live-in).
  bool reg_empty(unsigned RegNo) const { return reg_begin(RegNo) == reg_end(); }

  /// reg_nodbg_iterator/reg_nodbg_begin/reg_nodbg_end - Walk all defs and uses
  /// of the specified register, skipping those marked as Debug.
  using reg_nodbg_iterator =
      defusechain_iterator<true, true, true, true, false, false>;
  reg_nodbg_iterator reg_nodbg_begin(unsigned RegNo) const {
    return reg_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  static reg_nodbg_iterator reg_nodbg_end() {
    return reg_nodbg_iterator(nullptr);
  }

  inline iterator_range<reg_nodbg_iterator>
  reg_nodbg_operands(unsigned Reg) const {
    return make_range(reg_nodbg_begin(Reg), reg_nodbg_end());
  }

  /// reg_instr_nodbg_iterator/reg_instr_nodbg_begin/reg_instr_nodbg_end - Walk
  /// all defs and uses of the specified register, stepping by MachineInstr,
  /// skipping those marked as Debug.
  using reg_instr_nodbg_iterator =
      defusechain_instr_iterator<true, true, true, false, true, false>;
  reg_instr_nodbg_iterator reg_instr_nodbg_begin(unsigned RegNo) const {
    return reg_instr_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  static reg_instr_nodbg_iterator reg_instr_nodbg_end() {
    return reg_instr_nodbg_iterator(nullptr);
  }

  inline iterator_range<reg_instr_nodbg_iterator>
  reg_nodbg_instructions(unsigned Reg) const {
    return make_range(reg_instr_nodbg_begin(Reg), reg_instr_nodbg_end());
  }

  /// reg_bundle_nodbg_iterator/reg_bundle_nodbg_begin/reg_bundle_nodbg_end - Walk
  /// all defs and uses of the specified register, stepping by bundle,
  /// skipping those marked as Debug.
  using reg_bundle_nodbg_iterator =
      defusechain_instr_iterator<true, true, true, false, false, true>;
  reg_bundle_nodbg_iterator reg_bundle_nodbg_begin(unsigned RegNo) const {
    return reg_bundle_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  static reg_bundle_nodbg_iterator reg_bundle_nodbg_end() {
    return reg_bundle_nodbg_iterator(nullptr);
  }

  inline iterator_range<reg_bundle_nodbg_iterator>
  reg_nodbg_bundles(unsigned Reg) const {
    return make_range(reg_bundle_nodbg_begin(Reg), reg_bundle_nodbg_end());
  }

  /// reg_nodbg_empty - Return true if the only instructions using or defining
  /// Reg are Debug instructions.
  bool reg_nodbg_empty(unsigned RegNo) const {
    return reg_nodbg_begin(RegNo) == reg_nodbg_end();
  }

  /// def_iterator/def_begin/def_end - Walk all defs of the specified register.
  using def_iterator =
      defusechain_iterator<false, true, false, true, false, false>;
  def_iterator def_begin(unsigned RegNo) const {
    return def_iterator(getRegUseDefListHead(RegNo));
  }
  static def_iterator def_end() { return def_iterator(nullptr); }

  inline iterator_range<def_iterator> def_operands(unsigned Reg) const {
    return make_range(def_begin(Reg), def_end());
  }

  /// def_instr_iterator/def_instr_begin/def_instr_end - Walk all defs of the
  /// specified register, stepping by MachineInst.
  using def_instr_iterator =
      defusechain_instr_iterator<false, true, false, false, true, false>;
  def_instr_iterator def_instr_begin(unsigned RegNo) const {
    return def_instr_iterator(getRegUseDefListHead(RegNo));
  }
  static def_instr_iterator def_instr_end() {
    return def_instr_iterator(nullptr);
  }

  inline iterator_range<def_instr_iterator>
  def_instructions(unsigned Reg) const {
    return make_range(def_instr_begin(Reg), def_instr_end());
  }

  /// def_bundle_iterator/def_bundle_begin/def_bundle_end - Walk all defs of the
  /// specified register, stepping by bundle.
  using def_bundle_iterator =
      defusechain_instr_iterator<false, true, false, false, false, true>;
  def_bundle_iterator def_bundle_begin(unsigned RegNo) const {
    return def_bundle_iterator(getRegUseDefListHead(RegNo));
  }
  static def_bundle_iterator def_bundle_end() {
    return def_bundle_iterator(nullptr);
  }

  inline iterator_range<def_bundle_iterator> def_bundles(unsigned Reg) const {
    return make_range(def_bundle_begin(Reg), def_bundle_end());
  }

  /// def_empty - Return true if there are no instructions defining the
  /// specified register (it may be live-in).
  bool def_empty(unsigned RegNo) const { return def_begin(RegNo) == def_end(); }

  StringRef getVRegName(unsigned Reg) const {
    return VReg2Name.inBounds(Reg) ? StringRef(VReg2Name[Reg]) : "";
  }

  void insertVRegByName(StringRef Name, unsigned Reg) {
    assert((Name.empty() || VRegNames.find(Name) == VRegNames.end()) &&
           "Named VRegs Must be Unique.");
    if (!Name.empty()) {
      VRegNames.insert(Name);
      VReg2Name.grow(Reg);
      VReg2Name[Reg] = Name.str();
    }
  }

  /// Return true if there is exactly one operand defining the specified
  /// register.
  bool hasOneDef(unsigned RegNo) const {
    def_iterator DI = def_begin(RegNo);
    if (DI == def_end())
      return false;
    return ++DI == def_end();
  }

  /// use_iterator/use_begin/use_end - Walk all uses of the specified register.
  using use_iterator =
      defusechain_iterator<true, false, false, true, false, false>;
  use_iterator use_begin(unsigned RegNo) const {
    return use_iterator(getRegUseDefListHead(RegNo));
  }
  static use_iterator use_end() { return use_iterator(nullptr); }

  inline iterator_range<use_iterator> use_operands(unsigned Reg) const {
    return make_range(use_begin(Reg), use_end());
  }

  /// use_instr_iterator/use_instr_begin/use_instr_end - Walk all uses of the
  /// specified register, stepping by MachineInstr.
  using use_instr_iterator =
      defusechain_instr_iterator<true, false, false, false, true, false>;
  use_instr_iterator use_instr_begin(unsigned RegNo) const {
    return use_instr_iterator(getRegUseDefListHead(RegNo));
  }
  static use_instr_iterator use_instr_end() {
    return use_instr_iterator(nullptr);
  }

  inline iterator_range<use_instr_iterator>
  use_instructions(unsigned Reg) const {
    return make_range(use_instr_begin(Reg), use_instr_end());
  }

  /// use_bundle_iterator/use_bundle_begin/use_bundle_end - Walk all uses of the
  /// specified register, stepping by bundle.
  using use_bundle_iterator =
      defusechain_instr_iterator<true, false, false, false, false, true>;
  use_bundle_iterator use_bundle_begin(unsigned RegNo) const {
    return use_bundle_iterator(getRegUseDefListHead(RegNo));
  }
  static use_bundle_iterator use_bundle_end() {
    return use_bundle_iterator(nullptr);
  }

  inline iterator_range<use_bundle_iterator> use_bundles(unsigned Reg) const {
    return make_range(use_bundle_begin(Reg), use_bundle_end());
  }

  /// use_empty - Return true if there are no instructions using the specified
  /// register.
  bool use_empty(unsigned RegNo) const { return use_begin(RegNo) == use_end(); }

  /// hasOneUse - Return true if there is exactly one instruction using the
  /// specified register.
  bool hasOneUse(unsigned RegNo) const {
    use_iterator UI = use_begin(RegNo);
    if (UI == use_end())
      return false;
    return ++UI == use_end();
  }

  /// use_nodbg_iterator/use_nodbg_begin/use_nodbg_end - Walk all uses of the
  /// specified register, skipping those marked as Debug.
  using use_nodbg_iterator =
      defusechain_iterator<true, false, true, true, false, false>;
  use_nodbg_iterator use_nodbg_begin(unsigned RegNo) const {
    return use_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  static use_nodbg_iterator use_nodbg_end() {
    return use_nodbg_iterator(nullptr);
  }

  inline iterator_range<use_nodbg_iterator>
  use_nodbg_operands(unsigned Reg) const {
    return make_range(use_nodbg_begin(Reg), use_nodbg_end());
  }

  /// use_instr_nodbg_iterator/use_instr_nodbg_begin/use_instr_nodbg_end - Walk
  /// all uses of the specified register, stepping by MachineInstr, skipping
  /// those marked as Debug.
  using use_instr_nodbg_iterator =
      defusechain_instr_iterator<true, false, true, false, true, false>;
  use_instr_nodbg_iterator use_instr_nodbg_begin(unsigned RegNo) const {
    return use_instr_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  static use_instr_nodbg_iterator use_instr_nodbg_end() {
    return use_instr_nodbg_iterator(nullptr);
  }

  inline iterator_range<use_instr_nodbg_iterator>
  use_nodbg_instructions(unsigned Reg) const {
    return make_range(use_instr_nodbg_begin(Reg), use_instr_nodbg_end());
  }

  /// use_bundle_nodbg_iterator/use_bundle_nodbg_begin/use_bundle_nodbg_end - Walk
  /// all uses of the specified register, stepping by bundle, skipping
  /// those marked as Debug.
  using use_bundle_nodbg_iterator =
      defusechain_instr_iterator<true, false, true, false, false, true>;
  use_bundle_nodbg_iterator use_bundle_nodbg_begin(unsigned RegNo) const {
    return use_bundle_nodbg_iterator(getRegUseDefListHead(RegNo));
  }
  static use_bundle_nodbg_iterator use_bundle_nodbg_end() {
    return use_bundle_nodbg_iterator(nullptr);
  }

  inline iterator_range<use_bundle_nodbg_iterator>
  use_nodbg_bundles(unsigned Reg) const {
    return make_range(use_bundle_nodbg_begin(Reg), use_bundle_nodbg_end());
  }

  /// use_nodbg_empty - Return true if there are no non-Debug instructions
  /// using the specified register.
  bool use_nodbg_empty(unsigned RegNo) const {
    return use_nodbg_begin(RegNo) == use_nodbg_end();
  }

  /// hasOneNonDBGUse - Return true if there is exactly one non-Debug
  /// instruction using the specified register.
  bool hasOneNonDBGUse(unsigned RegNo) const;

  /// replaceRegWith - Replace all instances of FromReg with ToReg in the
  /// machine function.  This is like llvm-level X->replaceAllUsesWith(Y),
  /// except that it also changes any definitions of the register as well.
  ///
  /// Note that it is usually necessary to first constrain ToReg's register
  /// class and register bank to match the FromReg constraints using one of the
  /// methods:
  ///
  ///   constrainRegClass(ToReg, getRegClass(FromReg))
  ///   constrainRegAttrs(ToReg, FromReg)
  ///   RegisterBankInfo::constrainGenericRegister(ToReg,
  ///       *MRI.getRegClass(FromReg), MRI)
  ///
  /// These functions will return a falsy result if the virtual registers have
  /// incompatible constraints.
  ///
  /// Note that if ToReg is a physical register the function will replace and
  /// apply sub registers to ToReg in order to obtain a final/proper physical
  /// register.
  void replaceRegWith(unsigned FromReg, unsigned ToReg);

  /// getVRegDef - Return the machine instr that defines the specified virtual
  /// register or null if none is found.  This assumes that the code is in SSA
  /// form, so there should only be one definition.
  MachineInstr *getVRegDef(unsigned Reg) const;

  /// getUniqueVRegDef - Return the unique machine instr that defines the
  /// specified virtual register or null if none is found.  If there are
  /// multiple definitions or no definition, return null.
  MachineInstr *getUniqueVRegDef(unsigned Reg) const;

  /// clearKillFlags - Iterate over all the uses of the given register and
  /// clear the kill flag from the MachineOperand. This function is used by
  /// optimization passes which extend register lifetimes and need only
  /// preserve conservative kill flag information.
  void clearKillFlags(unsigned Reg) const;

  void dumpUses(unsigned RegNo) const;

  /// Returns true if PhysReg is unallocatable and constant throughout the
  /// function. Writing to a constant register has no effect.
  bool isConstantPhysReg(unsigned PhysReg) const;

  /// Returns true if either isConstantPhysReg or TRI->isCallerPreservedPhysReg
  /// returns true. This is a utility member function.
  bool isCallerPreservedOrConstPhysReg(unsigned PhysReg) const;

  /// Get an iterator over the pressure sets affected by the given physical or
  /// virtual register. If RegUnit is physical, it must be a register unit (from
  /// MCRegUnitIterator).
  PSetIterator getPressureSets(unsigned RegUnit) const;

  //===--------------------------------------------------------------------===//
  // Virtual Register Info
  //===--------------------------------------------------------------------===//

  /// Return the register class of the specified virtual register.
  /// This shouldn't be used directly unless \p Reg has a register class.
  /// \see getRegClassOrNull when this might happen.
  const TargetRegisterClass *getRegClass(unsigned Reg) const {
    assert(VRegInfo[Reg].first.is<const TargetRegisterClass *>() &&
           "Register class not set, wrong accessor");
    return VRegInfo[Reg].first.get<const TargetRegisterClass *>();
  }

  /// Return the register class of \p Reg, or null if Reg has not been assigned
  /// a register class yet.
  ///
  /// \note A null register class can only happen when these two
  /// conditions are met:
  /// 1. Generic virtual registers are created.
  /// 2. The machine function has not completely been through the
  ///    instruction selection process.
  /// None of this condition is possible without GlobalISel for now.
  /// In other words, if GlobalISel is not used or if the query happens after
  /// the select pass, using getRegClass is safe.
  const TargetRegisterClass *getRegClassOrNull(unsigned Reg) const {
    const RegClassOrRegBank &Val = VRegInfo[Reg].first;
    return Val.dyn_cast<const TargetRegisterClass *>();
  }

  /// Return the register bank of \p Reg, or null if Reg has not been assigned
  /// a register bank or has been assigned a register class.
  /// \note It is possible to get the register bank from the register class via
  /// RegisterBankInfo::getRegBankFromRegClass.
  const RegisterBank *getRegBankOrNull(unsigned Reg) const {
    const RegClassOrRegBank &Val = VRegInfo[Reg].first;
    return Val.dyn_cast<const RegisterBank *>();
  }

  /// Return the register bank or register class of \p Reg.
  /// \note Before the register bank gets assigned (i.e., before the
  /// RegBankSelect pass) \p Reg may not have either.
  const RegClassOrRegBank &getRegClassOrRegBank(unsigned Reg) const {
    return VRegInfo[Reg].first;
  }

  /// setRegClass - Set the register class of the specified virtual register.
  void setRegClass(unsigned Reg, const TargetRegisterClass *RC);

  /// Set the register bank to \p RegBank for \p Reg.
  void setRegBank(unsigned Reg, const RegisterBank &RegBank);

  void setRegClassOrRegBank(unsigned Reg,
                            const RegClassOrRegBank &RCOrRB){
    VRegInfo[Reg].first = RCOrRB;
  }

  /// constrainRegClass - Constrain the register class of the specified virtual
  /// register to be a common subclass of RC and the current register class,
  /// but only if the new class has at least MinNumRegs registers.  Return the
  /// new register class, or NULL if no such class exists.
  /// This should only be used when the constraint is known to be trivial, like
  /// GR32 -> GR32_NOSP. Beware of increasing register pressure.
  ///
  /// \note Assumes that the register has a register class assigned.
  /// Use RegisterBankInfo::constrainGenericRegister in GlobalISel's
  /// InstructionSelect pass and constrainRegAttrs in every other pass,
  /// including non-select passes of GlobalISel, instead.
  const TargetRegisterClass *constrainRegClass(unsigned Reg,
                                               const TargetRegisterClass *RC,
                                               unsigned MinNumRegs = 0);

  /// Constrain the register class or the register bank of the virtual register
  /// \p Reg (and low-level type) to be a common subclass or a common bank of
  /// both registers provided respectively (and a common low-level type). Do
  /// nothing if any of the attributes (classes, banks, or low-level types) of
  /// the registers are deemed incompatible, or if the resulting register will
  /// have a class smaller than before and of size less than \p MinNumRegs.
  /// Return true if such register attributes exist, false otherwise.
  ///
  /// \note Use this method instead of constrainRegClass and
  /// RegisterBankInfo::constrainGenericRegister everywhere but SelectionDAG
  /// ISel / FastISel and GlobalISel's InstructionSelect pass respectively.
  bool constrainRegAttrs(unsigned Reg, unsigned ConstrainingReg,
                         unsigned MinNumRegs = 0);

  /// recomputeRegClass - Try to find a legal super-class of Reg's register
  /// class that still satisfies the constraints from the instructions using
  /// Reg.  Returns true if Reg was upgraded.
  ///
  /// This method can be used after constraints have been removed from a
  /// virtual register, for example after removing instructions or splitting
  /// the live range.
  bool recomputeRegClass(unsigned Reg);

  /// createVirtualRegister - Create and return a new virtual register in the
  /// function with the specified register class.
  unsigned createVirtualRegister(const TargetRegisterClass *RegClass,
                                 StringRef Name = "");

  /// Create and return a new virtual register in the function with the same
  /// attributes as the given register.
  unsigned cloneVirtualRegister(unsigned VReg, StringRef Name = "");

  /// Get the low-level type of \p Reg or LLT{} if Reg is not a generic
  /// (target independent) virtual register.
  LLT getType(unsigned Reg) const {
    if (TargetRegisterInfo::isVirtualRegister(Reg) && VRegToType.inBounds(Reg))
      return VRegToType[Reg];
    return LLT{};
  }

  /// Set the low-level type of \p VReg to \p Ty.
  void setType(unsigned VReg, LLT Ty);

  /// Create and return a new generic virtual register with low-level
  /// type \p Ty.
  unsigned createGenericVirtualRegister(LLT Ty, StringRef Name = "");

  /// Remove all types associated to virtual registers (after instruction
  /// selection and constraining of all generic virtual registers).
  void clearVirtRegTypes();

  /// Creates a new virtual register that has no register class, register bank
  /// or size assigned yet. This is only allowed to be used
  /// temporarily while constructing machine instructions. Most operations are
  /// undefined on an incomplete register until one of setRegClass(),
  /// setRegBank() or setSize() has been called on it.
  unsigned createIncompleteVirtualRegister(StringRef Name = "");

  /// getNumVirtRegs - Return the number of virtual registers created.
  unsigned getNumVirtRegs() const { return VRegInfo.size(); }

  /// clearVirtRegs - Remove all virtual registers (after physreg assignment).
  void clearVirtRegs();

  /// setRegAllocationHint - Specify a register allocation hint for the
  /// specified virtual register. This is typically used by target, and in case
  /// of an earlier hint it will be overwritten.
  void setRegAllocationHint(unsigned VReg, unsigned Type, unsigned PrefReg) {
    assert(TargetRegisterInfo::isVirtualRegister(VReg));
    RegAllocHints[VReg].first  = Type;
    RegAllocHints[VReg].second.clear();
    RegAllocHints[VReg].second.push_back(PrefReg);
  }

  /// addRegAllocationHint - Add a register allocation hint to the hints
  /// vector for VReg.
  void addRegAllocationHint(unsigned VReg, unsigned PrefReg) {
    assert(TargetRegisterInfo::isVirtualRegister(VReg));
    RegAllocHints[VReg].second.push_back(PrefReg);
  }

  /// Specify the preferred (target independent) register allocation hint for
  /// the specified virtual register.
  void setSimpleHint(unsigned VReg, unsigned PrefReg) {
    setRegAllocationHint(VReg, /*Type=*/0, PrefReg);
  }

  void clearSimpleHint(unsigned VReg) {
    assert (RegAllocHints[VReg].first == 0 &&
            "Expected to clear a non-target hint!");
    RegAllocHints[VReg].second.clear();
  }

  /// getRegAllocationHint - Return the register allocation hint for the
  /// specified virtual register. If there are many hints, this returns the
  /// one with the greatest weight.
  std::pair<unsigned, unsigned>
  getRegAllocationHint(unsigned VReg) const {
    assert(TargetRegisterInfo::isVirtualRegister(VReg));
    unsigned BestHint = (RegAllocHints[VReg].second.size() ?
                         RegAllocHints[VReg].second[0] : 0);
    return std::pair<unsigned, unsigned>(RegAllocHints[VReg].first, BestHint);
  }

  /// getSimpleHint - same as getRegAllocationHint except it will only return
  /// a target independent hint.
  unsigned getSimpleHint(unsigned VReg) const {
    assert(TargetRegisterInfo::isVirtualRegister(VReg));
    std::pair<unsigned, unsigned> Hint = getRegAllocationHint(VReg);
    return Hint.first ? 0 : Hint.second;
  }

  /// getRegAllocationHints - Return a reference to the vector of all
  /// register allocation hints for VReg.
  const std::pair<unsigned, SmallVector<unsigned, 4>>
  &getRegAllocationHints(unsigned VReg) const {
    assert(TargetRegisterInfo::isVirtualRegister(VReg));
    return RegAllocHints[VReg];
  }

  /// markUsesInDebugValueAsUndef - Mark every DBG_VALUE referencing the
  /// specified register as undefined which causes the DBG_VALUE to be
  /// deleted during LiveDebugVariables analysis.
  void markUsesInDebugValueAsUndef(unsigned Reg) const;

  /// Return true if the specified register is modified in this function.
  /// This checks that no defining machine operands exist for the register or
  /// any of its aliases. Definitions found on functions marked noreturn are
  /// ignored, to consider them pass 'true' for optional parameter
  /// SkipNoReturnDef. The register is also considered modified when it is set
  /// in the UsedPhysRegMask.
  bool isPhysRegModified(unsigned PhysReg, bool SkipNoReturnDef = false) const;

  /// Return true if the specified register is modified or read in this
  /// function. This checks that no machine operands exist for the register or
  /// any of its aliases. The register is also considered used when it is set
  /// in the UsedPhysRegMask.
  bool isPhysRegUsed(unsigned PhysReg) const;

  /// addPhysRegsUsedFromRegMask - Mark any registers not in RegMask as used.
  /// This corresponds to the bit mask attached to register mask operands.
  void addPhysRegsUsedFromRegMask(const uint32_t *RegMask) {
    UsedPhysRegMask.setBitsNotInMask(RegMask);
  }

  const BitVector &getUsedPhysRegsMask() const { return UsedPhysRegMask; }

  //===--------------------------------------------------------------------===//
  // Reserved Register Info
  //===--------------------------------------------------------------------===//
  //
  // The set of reserved registers must be invariant during register
  // allocation.  For example, the target cannot suddenly decide it needs a
  // frame pointer when the register allocator has already used the frame
  // pointer register for something else.
  //
  // These methods can be used by target hooks like hasFP() to avoid changing
  // the reserved register set during register allocation.

  /// freezeReservedRegs - Called by the register allocator to freeze the set
  /// of reserved registers before allocation begins.
  void freezeReservedRegs(const MachineFunction&);

  /// reservedRegsFrozen - Returns true after freezeReservedRegs() was called
  /// to ensure the set of reserved registers stays constant.
  bool reservedRegsFrozen() const {
    return !ReservedRegs.empty();
  }

  /// canReserveReg - Returns true if PhysReg can be used as a reserved
  /// register.  Any register can be reserved before freezeReservedRegs() is
  /// called.
  bool canReserveReg(unsigned PhysReg) const {
    return !reservedRegsFrozen() || ReservedRegs.test(PhysReg);
  }

  /// getReservedRegs - Returns a reference to the frozen set of reserved
  /// registers. This method should always be preferred to calling
  /// TRI::getReservedRegs() when possible.
  const BitVector &getReservedRegs() const {
    assert(reservedRegsFrozen() &&
           "Reserved registers haven't been frozen yet. "
           "Use TRI::getReservedRegs().");
    return ReservedRegs;
  }

  /// isReserved - Returns true when PhysReg is a reserved register.
  ///
  /// Reserved registers may belong to an allocatable register class, but the
  /// target has explicitly requested that they are not used.
  bool isReserved(unsigned PhysReg) const {
    return getReservedRegs().test(PhysReg);
  }

  /// Returns true when the given register unit is considered reserved.
  ///
  /// Register units are considered reserved when for at least one of their
  /// root registers, the root register and all super registers are reserved.
  /// This currently iterates the register hierarchy and may be slower than
  /// expected.
  bool isReservedRegUnit(unsigned Unit) const;

  /// isAllocatable - Returns true when PhysReg belongs to an allocatable
  /// register class and it hasn't been reserved.
  ///
  /// Allocatable registers may show up in the allocation order of some virtual
  /// register, so a register allocator needs to track its liveness and
  /// availability.
  bool isAllocatable(unsigned PhysReg) const {
    return getTargetRegisterInfo()->isInAllocatableClass(PhysReg) &&
      !isReserved(PhysReg);
  }

  //===--------------------------------------------------------------------===//
  // LiveIn Management
  //===--------------------------------------------------------------------===//

  /// addLiveIn - Add the specified register as a live-in.  Note that it
  /// is an error to add the same register to the same set more than once.
  void addLiveIn(unsigned Reg, unsigned vreg = 0) {
    LiveIns.push_back(std::make_pair(Reg, vreg));
  }

  // Iteration support for the live-ins set.  It's kept in sorted order
  // by register number.
  using livein_iterator =
      std::vector<std::pair<unsigned,unsigned>>::const_iterator;
  livein_iterator livein_begin() const { return LiveIns.begin(); }
  livein_iterator livein_end()   const { return LiveIns.end(); }
  bool            livein_empty() const { return LiveIns.empty(); }

  ArrayRef<std::pair<unsigned, unsigned>> liveins() const {
    return LiveIns;
  }

  bool isLiveIn(unsigned Reg) const;

  /// getLiveInPhysReg - If VReg is a live-in virtual register, return the
  /// corresponding live-in physical register.
  unsigned getLiveInPhysReg(unsigned VReg) const;

  /// getLiveInVirtReg - If PReg is a live-in physical register, return the
  /// corresponding live-in physical register.
  unsigned getLiveInVirtReg(unsigned PReg) const;

  /// EmitLiveInCopies - Emit copies to initialize livein virtual registers
  /// into the given entry block.
  void EmitLiveInCopies(MachineBasicBlock *EntryMBB,
                        const TargetRegisterInfo &TRI,
                        const TargetInstrInfo &TII);

  /// Returns a mask covering all bits that can appear in lane masks of
  /// subregisters of the virtual register @p Reg.
  LaneBitmask getMaxLaneMaskForVReg(unsigned Reg) const;

  /// defusechain_iterator - This class provides iterator support for machine
  /// operands in the function that use or define a specific register.  If
  /// ReturnUses is true it returns uses of registers, if ReturnDefs is true it
  /// returns defs.  If neither are true then you are silly and it always
  /// returns end().  If SkipDebug is true it skips uses marked Debug
  /// when incrementing.
  template<bool ReturnUses, bool ReturnDefs, bool SkipDebug,
           bool ByOperand, bool ByInstr, bool ByBundle>
  class defusechain_iterator
    : public std::iterator<std::forward_iterator_tag, MachineInstr, ptrdiff_t> {
    friend class MachineRegisterInfo;

    MachineOperand *Op = nullptr;

    explicit defusechain_iterator(MachineOperand *op) : Op(op) {
      // If the first node isn't one we're interested in, advance to one that
      // we are interested in.
      if (op) {
        if ((!ReturnUses && op->isUse()) ||
            (!ReturnDefs && op->isDef()) ||
            (SkipDebug && op->isDebug()))
          advance();
      }
    }

    void advance() {
      assert(Op && "Cannot increment end iterator!");
      Op = getNextOperandForReg(Op);

      // All defs come before the uses, so stop def_iterator early.
      if (!ReturnUses) {
        if (Op) {
          if (Op->isUse())
            Op = nullptr;
          else
            assert(!Op->isDebug() && "Can't have debug defs");
        }
      } else {
        // If this is an operand we don't care about, skip it.
        while (Op && ((!ReturnDefs && Op->isDef()) ||
                      (SkipDebug && Op->isDebug())))
          Op = getNextOperandForReg(Op);
      }
    }

  public:
    using reference = std::iterator<std::forward_iterator_tag,
                                    MachineInstr, ptrdiff_t>::reference;
    using pointer = std::iterator<std::forward_iterator_tag,
                                  MachineInstr, ptrdiff_t>::pointer;

    defusechain_iterator() = default;

    bool operator==(const defusechain_iterator &x) const {
      return Op == x.Op;
    }
    bool operator!=(const defusechain_iterator &x) const {
      return !operator==(x);
    }

    /// atEnd - return true if this iterator is equal to reg_end() on the value.
    bool atEnd() const { return Op == nullptr; }

    // Iterator traversal: forward iteration only
    defusechain_iterator &operator++() {          // Preincrement
      assert(Op && "Cannot increment end iterator!");
      if (ByOperand)
        advance();
      else if (ByInstr) {
        MachineInstr *P = Op->getParent();
        do {
          advance();
        } while (Op && Op->getParent() == P);
      } else if (ByBundle) {
        MachineBasicBlock::instr_iterator P =
            getBundleStart(Op->getParent()->getIterator());
        do {
          advance();
        } while (Op && getBundleStart(Op->getParent()->getIterator()) == P);
      }

      return *this;
    }
    defusechain_iterator operator++(int) {        // Postincrement
      defusechain_iterator tmp = *this; ++*this; return tmp;
    }

    /// getOperandNo - Return the operand # of this MachineOperand in its
    /// MachineInstr.
    unsigned getOperandNo() const {
      assert(Op && "Cannot dereference end iterator!");
      return Op - &Op->getParent()->getOperand(0);
    }

    // Retrieve a reference to the current operand.
    MachineOperand &operator*() const {
      assert(Op && "Cannot dereference end iterator!");
      return *Op;
    }

    MachineOperand *operator->() const {
      assert(Op && "Cannot dereference end iterator!");
      return Op;
    }
  };

  /// defusechain_iterator - This class provides iterator support for machine
  /// operands in the function that use or define a specific register.  If
  /// ReturnUses is true it returns uses of registers, if ReturnDefs is true it
  /// returns defs.  If neither are true then you are silly and it always
  /// returns end().  If SkipDebug is true it skips uses marked Debug
  /// when incrementing.
  template<bool ReturnUses, bool ReturnDefs, bool SkipDebug,
           bool ByOperand, bool ByInstr, bool ByBundle>
  class defusechain_instr_iterator
    : public std::iterator<std::forward_iterator_tag, MachineInstr, ptrdiff_t> {
    friend class MachineRegisterInfo;

    MachineOperand *Op = nullptr;

    explicit defusechain_instr_iterator(MachineOperand *op) : Op(op) {
      // If the first node isn't one we're interested in, advance to one that
      // we are interested in.
      if (op) {
        if ((!ReturnUses && op->isUse()) ||
            (!ReturnDefs && op->isDef()) ||
            (SkipDebug && op->isDebug()))
          advance();
      }
    }

    void advance() {
      assert(Op && "Cannot increment end iterator!");
      Op = getNextOperandForReg(Op);

      // All defs come before the uses, so stop def_iterator early.
      if (!ReturnUses) {
        if (Op) {
          if (Op->isUse())
            Op = nullptr;
          else
            assert(!Op->isDebug() && "Can't have debug defs");
        }
      } else {
        // If this is an operand we don't care about, skip it.
        while (Op && ((!ReturnDefs && Op->isDef()) ||
                      (SkipDebug && Op->isDebug())))
          Op = getNextOperandForReg(Op);
      }
    }

  public:
    using reference = std::iterator<std::forward_iterator_tag,
                                    MachineInstr, ptrdiff_t>::reference;
    using pointer = std::iterator<std::forward_iterator_tag,
                                  MachineInstr, ptrdiff_t>::pointer;

    defusechain_instr_iterator() = default;

    bool operator==(const defusechain_instr_iterator &x) const {
      return Op == x.Op;
    }
    bool operator!=(const defusechain_instr_iterator &x) const {
      return !operator==(x);
    }

    /// atEnd - return true if this iterator is equal to reg_end() on the value.
    bool atEnd() const { return Op == nullptr; }

    // Iterator traversal: forward iteration only
    defusechain_instr_iterator &operator++() {          // Preincrement
      assert(Op && "Cannot increment end iterator!");
      if (ByOperand)
        advance();
      else if (ByInstr) {
        MachineInstr *P = Op->getParent();
        do {
          advance();
        } while (Op && Op->getParent() == P);
      } else if (ByBundle) {
        MachineBasicBlock::instr_iterator P =
            getBundleStart(Op->getParent()->getIterator());
        do {
          advance();
        } while (Op && getBundleStart(Op->getParent()->getIterator()) == P);
      }

      return *this;
    }
    defusechain_instr_iterator operator++(int) {        // Postincrement
      defusechain_instr_iterator tmp = *this; ++*this; return tmp;
    }

    // Retrieve a reference to the current operand.
    MachineInstr &operator*() const {
      assert(Op && "Cannot dereference end iterator!");
      if (ByBundle)
        return *getBundleStart(Op->getParent()->getIterator());
      return *Op->getParent();
    }

    MachineInstr *operator->() const { return &operator*(); }
  };
};

/// Iterate over the pressure sets affected by the given physical or virtual
/// register. If Reg is physical, it must be a register unit (from
/// MCRegUnitIterator).
class PSetIterator {
  const int *PSet = nullptr;
  unsigned Weight = 0;

public:
  PSetIterator() = default;

  PSetIterator(unsigned RegUnit, const MachineRegisterInfo *MRI) {
    const TargetRegisterInfo *TRI = MRI->getTargetRegisterInfo();
    if (TargetRegisterInfo::isVirtualRegister(RegUnit)) {
      const TargetRegisterClass *RC = MRI->getRegClass(RegUnit);
      PSet = TRI->getRegClassPressureSets(RC);
      Weight = TRI->getRegClassWeight(RC).RegWeight;
    }
    else {
      PSet = TRI->getRegUnitPressureSets(RegUnit);
      Weight = TRI->getRegUnitWeight(RegUnit);
    }
    if (*PSet == -1)
      PSet = nullptr;
  }

  bool isValid() const { return PSet; }

  unsigned getWeight() const { return Weight; }

  unsigned operator*() const { return *PSet; }

  void operator++() {
    assert(isValid() && "Invalid PSetIterator.");
    ++PSet;
    if (*PSet == -1)
      PSet = nullptr;
  }
};

inline PSetIterator MachineRegisterInfo::
getPressureSets(unsigned RegUnit) const {
  return PSetIterator(RegUnit, this);
}

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEREGISTERINFO_H
