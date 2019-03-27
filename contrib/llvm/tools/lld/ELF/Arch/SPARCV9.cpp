//===- SPARCV9.cpp --------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class SPARCV9 final : public TargetInfo {
public:
  SPARCV9();
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
  void writePlt(uint8_t *Buf, uint64_t GotEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;
};
} // namespace

SPARCV9::SPARCV9() {
  CopyRel = R_SPARC_COPY;
  GotRel = R_SPARC_GLOB_DAT;
  NoneRel = R_SPARC_NONE;
  PltRel = R_SPARC_JMP_SLOT;
  RelativeRel = R_SPARC_RELATIVE;
  GotEntrySize = 8;
  PltEntrySize = 32;
  PltHeaderSize = 4 * PltEntrySize;

  PageSize = 8192;
  DefaultMaxPageSize = 0x100000;
  DefaultImageBase = 0x100000;
}

RelExpr SPARCV9::getRelExpr(RelType Type, const Symbol &S,
                            const uint8_t *Loc) const {
  switch (Type) {
  case R_SPARC_32:
  case R_SPARC_UA32:
  case R_SPARC_64:
  case R_SPARC_UA64:
    return R_ABS;
  case R_SPARC_PC10:
  case R_SPARC_PC22:
  case R_SPARC_DISP32:
  case R_SPARC_WDISP30:
    return R_PC;
  case R_SPARC_GOT10:
    return R_GOT_OFF;
  case R_SPARC_GOT22:
    return R_GOT_OFF;
  case R_SPARC_WPLT30:
    return R_PLT_PC;
  case R_SPARC_NONE:
    return R_NONE;
  default:
    return R_INVALID;
  }
}

void SPARCV9::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_SPARC_32:
  case R_SPARC_UA32:
    // V-word32
    checkUInt(Loc, Val, 32, Type);
    write32be(Loc, Val);
    break;
  case R_SPARC_DISP32:
    // V-disp32
    checkInt(Loc, Val, 32, Type);
    write32be(Loc, Val);
    break;
  case R_SPARC_WDISP30:
  case R_SPARC_WPLT30:
    // V-disp30
    checkInt(Loc, Val, 32, Type);
    write32be(Loc, (read32be(Loc) & ~0x3fffffff) | ((Val >> 2) & 0x3fffffff));
    break;
  case R_SPARC_22:
    // V-imm22
    checkUInt(Loc, Val, 22, Type);
    write32be(Loc, (read32be(Loc) & ~0x003fffff) | (Val & 0x003fffff));
    break;
  case R_SPARC_GOT22:
  case R_SPARC_PC22:
    // T-imm22
    write32be(Loc, (read32be(Loc) & ~0x003fffff) | ((Val >> 10) & 0x003fffff));
    break;
  case R_SPARC_WDISP19:
    // V-disp19
    checkInt(Loc, Val, 21, Type);
    write32be(Loc, (read32be(Loc) & ~0x0007ffff) | ((Val >> 2) & 0x0007ffff));
    break;
  case R_SPARC_GOT10:
  case R_SPARC_PC10:
    // T-simm10
    write32be(Loc, (read32be(Loc) & ~0x000003ff) | (Val & 0x000003ff));
    break;
  case R_SPARC_64:
  case R_SPARC_UA64:
  case R_SPARC_GLOB_DAT:
    // V-xword64
    write64be(Loc, Val);
    break;
  default:
    error(getErrorLocation(Loc) + "unrecognized reloc " + Twine(Type));
  }
}

void SPARCV9::writePlt(uint8_t *Buf, uint64_t GotEntryAddr,
                       uint64_t PltEntryAddr, int32_t Index,
                       unsigned RelOff) const {
  const uint8_t PltData[] = {
      0x03, 0x00, 0x00, 0x00, // sethi   (. - .PLT0), %g1
      0x30, 0x68, 0x00, 0x00, // ba,a    %xcc, .PLT1
      0x01, 0x00, 0x00, 0x00, // nop
      0x01, 0x00, 0x00, 0x00, // nop
      0x01, 0x00, 0x00, 0x00, // nop
      0x01, 0x00, 0x00, 0x00, // nop
      0x01, 0x00, 0x00, 0x00, // nop
      0x01, 0x00, 0x00, 0x00  // nop
  };
  memcpy(Buf, PltData, sizeof(PltData));

  uint64_t Off = getPltEntryOffset(Index);
  relocateOne(Buf, R_SPARC_22, Off);
  relocateOne(Buf + 4, R_SPARC_WDISP19, -(Off + 4 - PltEntrySize));
}

TargetInfo *elf::getSPARCV9TargetInfo() {
  static SPARCV9 Target;
  return &Target;
}
