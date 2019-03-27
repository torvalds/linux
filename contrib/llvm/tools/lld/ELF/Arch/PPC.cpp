//===- PPC.cpp ------------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class PPC final : public TargetInfo {
public:
  PPC();
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
};
} // namespace

PPC::PPC() {
  NoneRel = R_PPC_NONE;
  GotBaseSymOff = 0x8000;
  GotBaseSymInGotPlt = false;
}

RelExpr PPC::getRelExpr(RelType Type, const Symbol &S,
                        const uint8_t *Loc) const {
  switch (Type) {
  case R_PPC_REL14:
  case R_PPC_REL24:
  case R_PPC_REL32:
    return R_PC;
  case R_PPC_PLTREL24:
    return R_PLT_PC;
  default:
    return R_ABS;
  }
}

void PPC::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_PPC_ADDR16_HA:
    write16be(Loc, (Val + 0x8000) >> 16);
    break;
  case R_PPC_ADDR16_HI:
    write16be(Loc, Val >> 16);
    break;
  case R_PPC_ADDR16_LO:
    write16be(Loc, Val);
    break;
  case R_PPC_ADDR32:
  case R_PPC_REL32:
    write32be(Loc, Val);
    break;
  case R_PPC_REL14:
    write32be(Loc, read32be(Loc) | (Val & 0xFFFC));
    break;
  case R_PPC_PLTREL24:
  case R_PPC_REL24:
    write32be(Loc, read32be(Loc) | (Val & 0x3FFFFFC));
    break;
  default:
    error(getErrorLocation(Loc) + "unrecognized reloc " + Twine(Type));
  }
}

TargetInfo *elf::getPPCTargetInfo() {
  static PPC Target;
  return &Target;
}
