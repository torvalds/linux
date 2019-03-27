//===-- llvm/MC/MCFixedLenDisassembler.h - Decoder driver -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Fixed length disassembler decoder state machine driver.
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCFIXEDLENDISASSEMBLER_H
#define LLVM_MC_MCFIXEDLENDISASSEMBLER_H

namespace llvm {

namespace MCD {
// Disassembler state machine opcodes.
enum DecoderOps {
  OPC_ExtractField = 1, // OPC_ExtractField(uint8_t Start, uint8_t Len)
  OPC_FilterValue,      // OPC_FilterValue(uleb128 Val, uint16_t NumToSkip)
  OPC_CheckField,       // OPC_CheckField(uint8_t Start, uint8_t Len,
                        //                uleb128 Val, uint16_t NumToSkip)
  OPC_CheckPredicate,   // OPC_CheckPredicate(uleb128 PIdx, uint16_t NumToSkip)
  OPC_Decode,           // OPC_Decode(uleb128 Opcode, uleb128 DIdx)
  OPC_TryDecode,        // OPC_TryDecode(uleb128 Opcode, uleb128 DIdx,
                        //               uint16_t NumToSkip)
  OPC_SoftFail,         // OPC_SoftFail(uleb128 PMask, uleb128 NMask)
  OPC_Fail              // OPC_Fail()
};

} // namespace MCDecode
} // namespace llvm

#endif
