//===- MC/MCRegisterInfo.cpp - Target Register Description ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements MCRegisterInfo functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace llvm;

unsigned MCRegisterInfo::getMatchingSuperReg(unsigned Reg, unsigned SubIdx,
                                             const MCRegisterClass *RC) const {
  for (MCSuperRegIterator Supers(Reg, this); Supers.isValid(); ++Supers)
    if (RC->contains(*Supers) && Reg == getSubReg(*Supers, SubIdx))
      return *Supers;
  return 0;
}

unsigned MCRegisterInfo::getSubReg(unsigned Reg, unsigned Idx) const {
  assert(Idx && Idx < getNumSubRegIndices() &&
         "This is not a subregister index");
  // Get a pointer to the corresponding SubRegIndices list. This list has the
  // name of each sub-register in the same order as MCSubRegIterator.
  const uint16_t *SRI = SubRegIndices + get(Reg).SubRegIndices;
  for (MCSubRegIterator Subs(Reg, this); Subs.isValid(); ++Subs, ++SRI)
    if (*SRI == Idx)
      return *Subs;
  return 0;
}

unsigned MCRegisterInfo::getSubRegIndex(unsigned Reg, unsigned SubReg) const {
  assert(SubReg && SubReg < getNumRegs() && "This is not a register");
  // Get a pointer to the corresponding SubRegIndices list. This list has the
  // name of each sub-register in the same order as MCSubRegIterator.
  const uint16_t *SRI = SubRegIndices + get(Reg).SubRegIndices;
  for (MCSubRegIterator Subs(Reg, this); Subs.isValid(); ++Subs, ++SRI)
    if (*Subs == SubReg)
      return *SRI;
  return 0;
}

unsigned MCRegisterInfo::getSubRegIdxSize(unsigned Idx) const {
  assert(Idx && Idx < getNumSubRegIndices() &&
         "This is not a subregister index");
  return SubRegIdxRanges[Idx].Size;
}

unsigned MCRegisterInfo::getSubRegIdxOffset(unsigned Idx) const {
  assert(Idx && Idx < getNumSubRegIndices() &&
         "This is not a subregister index");
  return SubRegIdxRanges[Idx].Offset;
}

int MCRegisterInfo::getDwarfRegNum(unsigned RegNum, bool isEH) const {
  const DwarfLLVMRegPair *M = isEH ? EHL2DwarfRegs : L2DwarfRegs;
  unsigned Size = isEH ? EHL2DwarfRegsSize : L2DwarfRegsSize;

  if (!M)
    return -1;
  DwarfLLVMRegPair Key = { RegNum, 0 };
  const DwarfLLVMRegPair *I = std::lower_bound(M, M+Size, Key);
  if (I == M+Size || I->FromReg != RegNum)
    return -1;
  return I->ToReg;
}

int MCRegisterInfo::getLLVMRegNum(unsigned RegNum, bool isEH) const {
  const DwarfLLVMRegPair *M = isEH ? EHDwarf2LRegs : Dwarf2LRegs;
  unsigned Size = isEH ? EHDwarf2LRegsSize : Dwarf2LRegsSize;

  if (!M)
    return -1;
  DwarfLLVMRegPair Key = { RegNum, 0 };
  const DwarfLLVMRegPair *I = std::lower_bound(M, M+Size, Key);
  assert(I != M+Size && I->FromReg == RegNum && "Invalid RegNum");
  return I->ToReg;
}

int MCRegisterInfo::getLLVMRegNumFromEH(unsigned RegNum) const {
  const DwarfLLVMRegPair *M = EHDwarf2LRegs;
  unsigned Size = EHDwarf2LRegsSize;

  if (!M)
    return -1;
  DwarfLLVMRegPair Key = { RegNum, 0 };
  const DwarfLLVMRegPair *I = std::lower_bound(M, M+Size, Key);
  if (I == M+Size || I->FromReg != RegNum)
    return -1;
  return I->ToReg;
}

int MCRegisterInfo::getDwarfRegNumFromDwarfEHRegNum(unsigned RegNum) const {
  // On ELF platforms, DWARF EH register numbers are the same as DWARF
  // other register numbers.  On Darwin x86, they differ and so need to be
  // mapped.  The .cfi_* directives accept integer literals as well as
  // register names and should generate exactly what the assembly code
  // asked for, so there might be DWARF/EH register numbers that don't have
  // a corresponding LLVM register number at all.  So if we can't map the
  // EH register number to an LLVM register number, assume it's just a
  // valid DWARF register number as is.
  int LRegNum = getLLVMRegNumFromEH(RegNum);
  if (LRegNum != -1)
    return getDwarfRegNum(LRegNum, false);
  return RegNum;
}

int MCRegisterInfo::getSEHRegNum(unsigned RegNum) const {
  const DenseMap<unsigned, int>::const_iterator I = L2SEHRegs.find(RegNum);
  if (I == L2SEHRegs.end()) return (int)RegNum;
  return I->second;
}

int MCRegisterInfo::getCodeViewRegNum(unsigned RegNum) const {
  if (L2CVRegs.empty())
    report_fatal_error("target does not implement codeview register mapping");
  const DenseMap<unsigned, int>::const_iterator I = L2CVRegs.find(RegNum);
  if (I == L2CVRegs.end())
    report_fatal_error("unknown codeview register " + (RegNum < getNumRegs()
                                                           ? getName(RegNum)
                                                           : Twine(RegNum)));
  return I->second;
}
