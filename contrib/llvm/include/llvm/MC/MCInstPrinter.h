//===- MCInstPrinter.h - MCInst to target assembly syntax -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTPRINTER_H
#define LLVM_MC_MCINSTPRINTER_H

#include "llvm/Support/Format.h"
#include <cstdint>

namespace llvm {

class MCAsmInfo;
class MCInst;
class MCInstrInfo;
class MCRegisterInfo;
class MCSubtargetInfo;
class raw_ostream;
class StringRef;

/// Convert `Bytes' to a hex string and output to `OS'
void dumpBytes(ArrayRef<uint8_t> Bytes, raw_ostream &OS);

namespace HexStyle {

enum Style {
  C,  ///< 0xff
  Asm ///< 0ffh
};

} // end namespace HexStyle

/// This is an instance of a target assembly language printer that
/// converts an MCInst to valid target assembly syntax.
class MCInstPrinter {
protected:
  /// A stream that comments can be emitted to if desired.  Each comment
  /// must end with a newline.  This will be null if verbose assembly emission
  /// is disabled.
  raw_ostream *CommentStream = nullptr;
  const MCAsmInfo &MAI;
  const MCInstrInfo &MII;
  const MCRegisterInfo &MRI;

  /// True if we are printing marked up assembly.
  bool UseMarkup = false;

  /// True if we are printing immediates as hex.
  bool PrintImmHex = false;

  /// Which style to use for printing hexadecimal values.
  HexStyle::Style PrintHexStyle = HexStyle::C;

  /// Utility function for printing annotations.
  void printAnnotation(raw_ostream &OS, StringRef Annot);

public:
  MCInstPrinter(const MCAsmInfo &mai, const MCInstrInfo &mii,
                const MCRegisterInfo &mri) : MAI(mai), MII(mii), MRI(mri) {}

  virtual ~MCInstPrinter();

  /// Specify a stream to emit comments to.
  void setCommentStream(raw_ostream &OS) { CommentStream = &OS; }

  /// Print the specified MCInst to the specified raw_ostream.
  virtual void printInst(const MCInst *MI, raw_ostream &OS, StringRef Annot,
                         const MCSubtargetInfo &STI) = 0;

  /// Return the name of the specified opcode enum (e.g. "MOV32ri") or
  /// empty if we can't resolve it.
  StringRef getOpcodeName(unsigned Opcode) const;

  /// Print the assembler register name.
  virtual void printRegName(raw_ostream &OS, unsigned RegNo) const;

  bool getUseMarkup() const { return UseMarkup; }
  void setUseMarkup(bool Value) { UseMarkup = Value; }

  /// Utility functions to make adding mark ups simpler.
  StringRef markup(StringRef s) const;
  StringRef markup(StringRef a, StringRef b) const;

  bool getPrintImmHex() const { return PrintImmHex; }
  void setPrintImmHex(bool Value) { PrintImmHex = Value; }

  HexStyle::Style getPrintHexStyle() const { return PrintHexStyle; }
  void setPrintHexStyle(HexStyle::Style Value) { PrintHexStyle = Value; }

  /// Utility function to print immediates in decimal or hex.
  format_object<int64_t> formatImm(int64_t Value) const {
    return PrintImmHex ? formatHex(Value) : formatDec(Value);
  }

  /// Utility functions to print decimal/hexadecimal values.
  format_object<int64_t> formatDec(int64_t Value) const;
  format_object<int64_t> formatHex(int64_t Value) const;
  format_object<uint64_t> formatHex(uint64_t Value) const;
};

} // end namespace llvm

#endif // LLVM_MC_MCINSTPRINTER_H
