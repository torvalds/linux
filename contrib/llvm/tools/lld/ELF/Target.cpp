//===- Target.cpp ---------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Machine-specific things, such as applying relocations, creation of
// GOT or PLT entries, etc., are handled in this file.
//
// Refer the ELF spec for the single letter variables, S, A or P, used
// in this file.
//
// Some functions defined in this file has "relaxTls" as part of their names.
// They do peephole optimization for TLS variables by rewriting instructions.
// They are not part of the ABI but optional optimization, so you can skip
// them if you are not interested in how TLS variables are optimized.
// See the following paper for the details.
//
//   Ulrich Drepper, ELF Handling For Thread-Local Storage
//   http://www.akkadia.org/drepper/tls.pdf
//
//===----------------------------------------------------------------------===//

#include "Target.h"
#include "InputFiles.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Object/ELF.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

TargetInfo *elf::Target;

std::string lld::toString(RelType Type) {
  StringRef S = getELFRelocationTypeName(elf::Config->EMachine, Type);
  if (S == "Unknown")
    return ("Unknown (" + Twine(Type) + ")").str();
  return S;
}

TargetInfo *elf::getTarget() {
  switch (Config->EMachine) {
  case EM_386:
  case EM_IAMCU:
    return getX86TargetInfo();
  case EM_AARCH64:
    return getAArch64TargetInfo();
  case EM_AMDGPU:
    return getAMDGPUTargetInfo();
  case EM_ARM:
    return getARMTargetInfo();
  case EM_AVR:
    return getAVRTargetInfo();
  case EM_HEXAGON:
    return getHexagonTargetInfo();
  case EM_MIPS:
    switch (Config->EKind) {
    case ELF32LEKind:
      return getMipsTargetInfo<ELF32LE>();
    case ELF32BEKind:
      return getMipsTargetInfo<ELF32BE>();
    case ELF64LEKind:
      return getMipsTargetInfo<ELF64LE>();
    case ELF64BEKind:
      return getMipsTargetInfo<ELF64BE>();
    default:
      llvm_unreachable("unsupported MIPS target");
    }
  case EM_MSP430:
    return getMSP430TargetInfo();
  case EM_PPC:
    return getPPCTargetInfo();
  case EM_PPC64:
    return getPPC64TargetInfo();
  case EM_RISCV:
    return getRISCVTargetInfo();
  case EM_SPARCV9:
    return getSPARCV9TargetInfo();
  case EM_X86_64:
    if (Config->EKind == ELF32LEKind)
      return getX32TargetInfo();
    return getX86_64TargetInfo();
  }
  llvm_unreachable("unknown target machine");
}

template <class ELFT> static ErrorPlace getErrPlace(const uint8_t *Loc) {
  for (InputSectionBase *D : InputSections) {
    auto *IS = cast<InputSection>(D);
    if (!IS->getParent())
      continue;

    uint8_t *ISLoc = IS->getParent()->Loc + IS->OutSecOff;
    if (ISLoc <= Loc && Loc < ISLoc + IS->getSize())
      return {IS, IS->template getLocation<ELFT>(Loc - ISLoc) + ": "};
  }
  return {};
}

ErrorPlace elf::getErrorPlace(const uint8_t *Loc) {
  switch (Config->EKind) {
  case ELF32LEKind:
    return getErrPlace<ELF32LE>(Loc);
  case ELF32BEKind:
    return getErrPlace<ELF32BE>(Loc);
  case ELF64LEKind:
    return getErrPlace<ELF64LE>(Loc);
  case ELF64BEKind:
    return getErrPlace<ELF64BE>(Loc);
  default:
    llvm_unreachable("unknown ELF type");
  }
}

TargetInfo::~TargetInfo() {}

int64_t TargetInfo::getImplicitAddend(const uint8_t *Buf, RelType Type) const {
  return 0;
}

bool TargetInfo::usesOnlyLowPageBits(RelType Type) const { return false; }

bool TargetInfo::needsThunk(RelExpr Expr, RelType Type, const InputFile *File,
                            uint64_t BranchAddr, const Symbol &S) const {
  return false;
}

bool TargetInfo::adjustPrologueForCrossSplitStack(uint8_t *Loc, uint8_t *End,
                                                  uint8_t StOther) const {
  llvm_unreachable("Target doesn't support split stacks.");
}

bool TargetInfo::inBranchRange(RelType Type, uint64_t Src, uint64_t Dst) const {
  return true;
}

void TargetInfo::writeIgotPlt(uint8_t *Buf, const Symbol &S) const {
  writeGotPlt(Buf, S);
}

RelExpr TargetInfo::adjustRelaxExpr(RelType Type, const uint8_t *Data,
                                    RelExpr Expr) const {
  return Expr;
}

void TargetInfo::relaxGot(uint8_t *Loc, uint64_t Val) const {
  llvm_unreachable("Should not have claimed to be relaxable");
}

void TargetInfo::relaxTlsGdToLe(uint8_t *Loc, RelType Type,
                                uint64_t Val) const {
  llvm_unreachable("Should not have claimed to be relaxable");
}

void TargetInfo::relaxTlsGdToIe(uint8_t *Loc, RelType Type,
                                uint64_t Val) const {
  llvm_unreachable("Should not have claimed to be relaxable");
}

void TargetInfo::relaxTlsIeToLe(uint8_t *Loc, RelType Type,
                                uint64_t Val) const {
  llvm_unreachable("Should not have claimed to be relaxable");
}

void TargetInfo::relaxTlsLdToLe(uint8_t *Loc, RelType Type,
                                uint64_t Val) const {
  llvm_unreachable("Should not have claimed to be relaxable");
}

uint64_t TargetInfo::getImageBase() {
  // Use -image-base if set. Fall back to the target default if not.
  if (Config->ImageBase)
    return *Config->ImageBase;
  return Config->Pic ? 0 : DefaultImageBase;
}
