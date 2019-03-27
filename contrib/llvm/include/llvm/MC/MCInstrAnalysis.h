//===- llvm/MC/MCInstrAnalysis.h - InstrDesc target hooks -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MCInstrAnalysis class which the MCTargetDescs can
// derive from to give additional information to MC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTRANALYSIS_H
#define LLVM_MC_MCINSTRANALYSIS_H

#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include <cstdint>

namespace llvm {

class MCRegisterInfo;
class Triple;

class MCInstrAnalysis {
protected:
  friend class Target;

  const MCInstrInfo *Info;

public:
  MCInstrAnalysis(const MCInstrInfo *Info) : Info(Info) {}
  virtual ~MCInstrAnalysis() = default;

  virtual bool isBranch(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isBranch();
  }

  virtual bool isConditionalBranch(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isConditionalBranch();
  }

  virtual bool isUnconditionalBranch(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isUnconditionalBranch();
  }

  virtual bool isIndirectBranch(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isIndirectBranch();
  }

  virtual bool isCall(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isCall();
  }

  virtual bool isReturn(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isReturn();
  }

  virtual bool isTerminator(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isTerminator();
  }

  /// Returns true if at least one of the register writes performed by
  /// \param Inst implicitly clears the upper portion of all super-registers.
  ///
  /// Example: on X86-64, a write to EAX implicitly clears the upper half of
  /// RAX. Also (still on x86) an XMM write perfomed by an AVX 128-bit
  /// instruction implicitly clears the upper portion of the correspondent
  /// YMM register.
  ///
  /// This method also updates an APInt which is used as mask of register
  /// writes. There is one bit for every explicit/implicit write performed by
  /// the instruction. If a write implicitly clears its super-registers, then
  /// the corresponding bit is set (vic. the corresponding bit is cleared).
  ///
  /// The first bits in the APint are related to explicit writes. The remaining
  /// bits are related to implicit writes. The sequence of writes follows the
  /// machine operand sequence. For implicit writes, the sequence is defined by
  /// the MCInstrDesc.
  ///
  /// The assumption is that the bit-width of the APInt is correctly set by
  /// the caller. The default implementation conservatively assumes that none of
  /// the writes clears the upper portion of a super-register.
  virtual bool clearsSuperRegisters(const MCRegisterInfo &MRI,
                                    const MCInst &Inst,
                                    APInt &Writes) const;

  /// Returns true if MI is a dependency breaking zero-idiom for the given
  /// subtarget.
  ///
  /// Mask is used to identify input operands that have their dependency
  /// broken. Each bit of the mask is associated with a specific input operand.
  /// Bits associated with explicit input operands are laid out first in the
  /// mask; implicit operands come after explicit operands.
  /// 
  /// Dependencies are broken only for operands that have their corresponding bit
  /// set. Operands that have their bit cleared, or that don't have a
  /// corresponding bit in the mask don't have their dependency broken.  Note
  /// that Mask may not be big enough to describe all operands.  The assumption
  /// for operands that don't have a correspondent bit in the mask is that those
  /// are still data dependent.
  /// 
  /// The only exception to the rule is for when Mask has all zeroes.
  /// A zero mask means: dependencies are broken for all explicit register
  /// operands.
  virtual bool isZeroIdiom(const MCInst &MI, APInt &Mask,
                           unsigned CPUID) const {
    return false;
  }

  /// Returns true if MI is a dependency breaking instruction for the
  /// subtarget associated with CPUID .
  ///
  /// The value computed by a dependency breaking instruction is not dependent
  /// on the inputs. An example of dependency breaking instruction on X86 is
  /// `XOR %eax, %eax`.
  ///
  /// If MI is a dependency breaking instruction for subtarget CPUID, then Mask
  /// can be inspected to identify independent operands.
  ///
  /// Essentially, each bit of the mask corresponds to an input operand.
  /// Explicit operands are laid out first in the mask; implicit operands follow
  /// explicit operands. Bits are set for operands that are independent.
  ///
  /// Note that the number of bits in Mask may not be equivalent to the sum of
  /// explicit and implicit operands in MI. Operands that don't have a
  /// corresponding bit in Mask are assumed "not independente".
  ///
  /// The only exception is for when Mask is all zeroes. That means: explicit
  /// input operands of MI are independent.
  virtual bool isDependencyBreaking(const MCInst &MI, APInt &Mask,
                                    unsigned CPUID) const {
    return isZeroIdiom(MI, Mask, CPUID);
  }

  /// Returns true if MI is a candidate for move elimination.
  ///
  /// Different subtargets may apply different constraints to optimizable
  /// register moves. For example, on most X86 subtargets, a candidate for move
  /// elimination cannot specify the same register for both source and
  /// destination.
  virtual bool isOptimizableRegisterMove(const MCInst &MI,
                                         unsigned CPUID) const {
    return false;
  }

  /// Given a branch instruction try to get the address the branch
  /// targets. Return true on success, and the address in Target.
  virtual bool
  evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                 uint64_t &Target) const;

  /// Returns (PLT virtual address, GOT virtual address) pairs for PLT entries.
  virtual std::vector<std::pair<uint64_t, uint64_t>>
  findPltEntries(uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents,
                 uint64_t GotPltSectionVA, const Triple &TargetTriple) const {
    return {};
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCINSTRANALYSIS_H
