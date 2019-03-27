//==-- llvm/CodeGen/GlobalISel/RegisterBank.h - Register Bank ----*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API of register banks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_REGBANK_H
#define LLVM_CODEGEN_GLOBALISEL_REGBANK_H

#include "llvm/ADT/BitVector.h"

namespace llvm {
// Forward declarations.
class RegisterBankInfo;
class raw_ostream;
class TargetRegisterClass;
class TargetRegisterInfo;

/// This class implements the register bank concept.
/// Two instances of RegisterBank must have different ID.
/// This property is enforced by the RegisterBankInfo class.
class RegisterBank {
private:
  unsigned ID;
  const char *Name;
  unsigned Size;
  BitVector ContainedRegClasses;

  /// Sentinel value used to recognize register bank not properly
  /// initialized yet.
  static const unsigned InvalidID;

  /// Only the RegisterBankInfo can initialize RegisterBank properly.
  friend RegisterBankInfo;

public:
  RegisterBank(unsigned ID, const char *Name, unsigned Size,
               const uint32_t *CoveredClasses, unsigned NumRegClasses);

  /// Get the identifier of this register bank.
  unsigned getID() const { return ID; }

  /// Get a user friendly name of this register bank.
  /// Should be used only for debugging purposes.
  const char *getName() const { return Name; }

  /// Get the maximal size in bits that fits in this register bank.
  unsigned getSize() const { return Size; }

  /// Check whether this instance is ready to be used.
  bool isValid() const;

  /// Check if this register bank is valid. In other words,
  /// if it has been properly constructed.
  ///
  /// \note This method does not check anything when assertions are disabled.
  ///
  /// \return True is the check was successful.
  bool verify(const TargetRegisterInfo &TRI) const;

  /// Check whether this register bank covers \p RC.
  /// In other words, check if this register bank fully covers
  /// the registers that \p RC contains.
  /// \pre isValid()
  bool covers(const TargetRegisterClass &RC) const;

  /// Check whether \p OtherRB is the same as this.
  bool operator==(const RegisterBank &OtherRB) const;
  bool operator!=(const RegisterBank &OtherRB) const {
    return !this->operator==(OtherRB);
  }

  /// Dump the register mask on dbgs() stream.
  /// The dump is verbose.
  void dump(const TargetRegisterInfo *TRI = nullptr) const;

  /// Print the register mask on OS.
  /// If IsForDebug is false, then only the name of the register bank
  /// is printed. Otherwise, all the fields are printing.
  /// TRI is then used to print the name of the register classes that
  /// this register bank covers.
  void print(raw_ostream &OS, bool IsForDebug = false,
             const TargetRegisterInfo *TRI = nullptr) const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const RegisterBank &RegBank) {
  RegBank.print(OS);
  return OS;
}
} // End namespace llvm.

#endif
