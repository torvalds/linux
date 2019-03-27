//===-- VIInstrFormats.td - VI Instruction Encodings ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// VI Instruction format definitions.
//
//===----------------------------------------------------------------------===//

class EXPe_vi : EXPe {
  let Inst{31-26} = 0x31; //encoding
}

class VINTRPe_vi <bits<2> op> : VINTRPe <op> {
  let Inst{31-26} = 0x35; // encoding
}
