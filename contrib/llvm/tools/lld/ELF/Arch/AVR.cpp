//===- AVR.cpp ------------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// AVR is a Harvard-architecture 8-bit micrcontroller designed for small
// baremetal programs. All AVR-family processors have 32 8-bit registers.
// The tiniest AVR has 32 byte RAM and 1 KiB program memory, and the largest
// one supports up to 2^24 data address space and 2^22 code address space.
//
// Since it is a baremetal programming, there's usually no loader to load
// ELF files on AVRs. You are expected to link your program against address
// 0 and pull out a .text section from the result using objcopy, so that you
// can write the linked code to on-chip flush memory. You can do that with
// the following commands:
//
//   ld.lld -Ttext=0 -o foo foo.o
//   objcopy -O binary --only-section=.text foo output.bin
//
// Note that the current AVR support is very preliminary so you can't
// link any useful program yet, though.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class AVR final : public TargetInfo {
public:
  AVR();
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;
};
} // namespace

AVR::AVR() { NoneRel = R_AVR_NONE; }

RelExpr AVR::getRelExpr(RelType Type, const Symbol &S,
                        const uint8_t *Loc) const {
  return R_ABS;
}

void AVR::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_AVR_CALL: {
    uint16_t Hi = Val >> 17;
    uint16_t Lo = Val >> 1;
    write16le(Loc, read16le(Loc) | ((Hi >> 1) << 4) | (Hi & 1));
    write16le(Loc + 2, Lo);
    break;
  }
  default:
    error(getErrorLocation(Loc) + "unrecognized reloc " + toString(Type));
  }
}

TargetInfo *elf::getAVRTargetInfo() {
  static AVR Target;
  return &Target;
}
