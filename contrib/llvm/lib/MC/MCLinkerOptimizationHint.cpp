//===- llvm/MC/MCLinkerOptimizationHint.cpp ----- LOH handling ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>

using namespace llvm;

// Each LOH is composed by, in this order (each field is encoded using ULEB128):
// - Its kind.
// - Its number of arguments (let say N).
// - Its arg1.
// - ...
// - Its argN.
// <arg1> to <argN> are absolute addresses in the object file, i.e.,
// relative addresses from the beginning of the object file.
void MCLOHDirective::emit_impl(raw_ostream &OutStream,
                               const MachObjectWriter &ObjWriter,
                               const MCAsmLayout &Layout) const {
  encodeULEB128(Kind, OutStream);
  encodeULEB128(Args.size(), OutStream);
  for (const MCSymbol *Arg : Args)
    encodeULEB128(ObjWriter.getSymbolAddress(*Arg, Layout), OutStream);
}

void MCLOHDirective::emit(MachObjectWriter &ObjWriter,
                          const MCAsmLayout &Layout) const {
  raw_ostream &OutStream = ObjWriter.W.OS;
  emit_impl(OutStream, ObjWriter, Layout);
}

uint64_t MCLOHDirective::getEmitSize(const MachObjectWriter &ObjWriter,
                                     const MCAsmLayout &Layout) const {
  class raw_counting_ostream : public raw_ostream {
    uint64_t Count = 0;

    void write_impl(const char *, size_t size) override { Count += size; }

    uint64_t current_pos() const override { return Count; }

  public:
    raw_counting_ostream() = default;
    ~raw_counting_ostream() override { flush(); }
  };

  raw_counting_ostream OutStream;
  emit_impl(OutStream, ObjWriter, Layout);
  return OutStream.tell();
}
