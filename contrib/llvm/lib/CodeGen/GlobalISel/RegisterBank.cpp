//===- llvm/CodeGen/GlobalISel/RegisterBank.cpp - Register Bank --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the RegisterBank class.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/RegisterBank.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Config/llvm-config.h"

#define DEBUG_TYPE "registerbank"

using namespace llvm;

const unsigned RegisterBank::InvalidID = UINT_MAX;

RegisterBank::RegisterBank(
    unsigned ID, const char *Name, unsigned Size,
    const uint32_t *CoveredClasses, unsigned NumRegClasses)
    : ID(ID), Name(Name), Size(Size) {
  ContainedRegClasses.resize(NumRegClasses);
  ContainedRegClasses.setBitsInMask(CoveredClasses);
}

bool RegisterBank::verify(const TargetRegisterInfo &TRI) const {
  assert(isValid() && "Invalid register bank");
  for (unsigned RCId = 0, End = TRI.getNumRegClasses(); RCId != End; ++RCId) {
    const TargetRegisterClass &RC = *TRI.getRegClass(RCId);

    if (!covers(RC))
      continue;
    // Verify that the register bank covers all the sub classes of the
    // classes it covers.

    // Use a different (slow in that case) method than
    // RegisterBankInfo to find the subclasses of RC, to make sure
    // both agree on the covers.
    for (unsigned SubRCId = 0; SubRCId != End; ++SubRCId) {
      const TargetRegisterClass &SubRC = *TRI.getRegClass(RCId);

      if (!RC.hasSubClassEq(&SubRC))
        continue;

      // Verify that the Size of the register bank is big enough to cover
      // all the register classes it covers.
      assert(getSize() >= TRI.getRegSizeInBits(SubRC) &&
             "Size is not big enough for all the subclasses!");
      assert(covers(SubRC) && "Not all subclasses are covered");
    }
  }
  return true;
}

bool RegisterBank::covers(const TargetRegisterClass &RC) const {
  assert(isValid() && "RB hasn't been initialized yet");
  return ContainedRegClasses.test(RC.getID());
}

bool RegisterBank::isValid() const {
  return ID != InvalidID && Name != nullptr && Size != 0 &&
         // A register bank that does not cover anything is useless.
         !ContainedRegClasses.empty();
}

bool RegisterBank::operator==(const RegisterBank &OtherRB) const {
  // There must be only one instance of a given register bank alive
  // for the whole compilation.
  // The RegisterBankInfo is supposed to enforce that.
  assert((OtherRB.getID() != getID() || &OtherRB == this) &&
         "ID does not uniquely identify a RegisterBank");
  return &OtherRB == this;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RegisterBank::dump(const TargetRegisterInfo *TRI) const {
  print(dbgs(), /* IsForDebug */ true, TRI);
}
#endif

void RegisterBank::print(raw_ostream &OS, bool IsForDebug,
                         const TargetRegisterInfo *TRI) const {
  OS << getName();
  if (!IsForDebug)
    return;
  OS << "(ID:" << getID() << ", Size:" << getSize() << ")\n"
     << "isValid:" << isValid() << '\n'
     << "Number of Covered register classes: " << ContainedRegClasses.count()
     << '\n';
  // Print all the subclasses if we can.
  // This register classes may not be properly initialized yet.
  if (!TRI || ContainedRegClasses.empty())
    return;
  assert(ContainedRegClasses.size() == TRI->getNumRegClasses() &&
         "TRI does not match the initialization process?");
  bool IsFirst = true;
  OS << "Covered register classes:\n";
  for (unsigned RCId = 0, End = TRI->getNumRegClasses(); RCId != End; ++RCId) {
    const TargetRegisterClass &RC = *TRI->getRegClass(RCId);

    if (!covers(RC))
      continue;

    if (!IsFirst)
      OS << ", ";
    OS << TRI->getRegClassName(&RC);
    IsFirst = false;
  }
}
