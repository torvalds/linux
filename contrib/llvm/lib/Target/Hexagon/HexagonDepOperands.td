//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Automatically generated file, please consult code owner before editing.
//===----------------------------------------------------------------------===//

def s4_0ImmOperand : AsmOperandClass { let Name = "s4_0Imm"; let RenderMethod = "addSignedImmOperands"; }
def s4_0Imm : Operand<i32> { let ParserMatchClass = s4_0ImmOperand; let DecoderMethod = "s4_0ImmDecoder"; }
def s4_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<4, 0>(N->getSExtValue());}]>;
def s29_3ImmOperand : AsmOperandClass { let Name = "s29_3Imm"; let RenderMethod = "addSignedImmOperands"; }
def s29_3Imm : Operand<i32> { let ParserMatchClass = s29_3ImmOperand; let DecoderMethod = "s29_3ImmDecoder"; }
def s29_3ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<32, 3>(N->getSExtValue());}]>;
def u6_0ImmOperand : AsmOperandClass { let Name = "u6_0Imm"; let RenderMethod = "addImmOperands"; }
def u6_0Imm : Operand<i32> { let ParserMatchClass = u6_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u6_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<6, 0>(N->getSExtValue());}]>;
def a30_2ImmOperand : AsmOperandClass { let Name = "a30_2Imm"; let RenderMethod = "addSignedImmOperands"; }
def a30_2Imm : Operand<i32> { let ParserMatchClass = a30_2ImmOperand; let DecoderMethod = "brtargetDecoder"; let PrintMethod = "printBrtarget"; }
def a30_2ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<32, 2>(N->getSExtValue());}]>;
def u29_3ImmOperand : AsmOperandClass { let Name = "u29_3Imm"; let RenderMethod = "addImmOperands"; }
def u29_3Imm : Operand<i32> { let ParserMatchClass = u29_3ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u29_3ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<32, 3>(N->getSExtValue());}]>;
def s8_0ImmOperand : AsmOperandClass { let Name = "s8_0Imm"; let RenderMethod = "addSignedImmOperands"; }
def s8_0Imm : Operand<i32> { let ParserMatchClass = s8_0ImmOperand; let DecoderMethod = "s8_0ImmDecoder"; }
def s8_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<8, 0>(N->getSExtValue());}]>;
def u32_0ImmOperand : AsmOperandClass { let Name = "u32_0Imm"; let RenderMethod = "addImmOperands"; }
def u32_0Imm : Operand<i32> { let ParserMatchClass = u32_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u32_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<32, 0>(N->getSExtValue());}]>;
def u4_2ImmOperand : AsmOperandClass { let Name = "u4_2Imm"; let RenderMethod = "addImmOperands"; }
def u4_2Imm : Operand<i32> { let ParserMatchClass = u4_2ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u4_2ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<4, 2>(N->getSExtValue());}]>;
def u3_0ImmOperand : AsmOperandClass { let Name = "u3_0Imm"; let RenderMethod = "addImmOperands"; }
def u3_0Imm : Operand<i32> { let ParserMatchClass = u3_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u3_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<3, 0>(N->getSExtValue());}]>;
def b15_2ImmOperand : AsmOperandClass { let Name = "b15_2Imm"; let RenderMethod = "addSignedImmOperands"; }
def b15_2Imm : Operand<OtherVT> { let ParserMatchClass = b15_2ImmOperand; let DecoderMethod = "brtargetDecoder"; let PrintMethod = "printBrtarget"; }
def b15_2ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<15, 2>(N->getSExtValue());}]>;
def u11_3ImmOperand : AsmOperandClass { let Name = "u11_3Imm"; let RenderMethod = "addImmOperands"; }
def u11_3Imm : Operand<i32> { let ParserMatchClass = u11_3ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u11_3ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<11, 3>(N->getSExtValue());}]>;
def s4_3ImmOperand : AsmOperandClass { let Name = "s4_3Imm"; let RenderMethod = "addSignedImmOperands"; }
def s4_3Imm : Operand<i32> { let ParserMatchClass = s4_3ImmOperand; let DecoderMethod = "s4_3ImmDecoder"; }
def s4_3ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<4, 3>(N->getSExtValue());}]>;
def m32_0ImmOperand : AsmOperandClass { let Name = "m32_0Imm"; let RenderMethod = "addImmOperands"; }
def m32_0Imm : Operand<i32> { let ParserMatchClass = m32_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def m32_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<32, 0>(N->getSExtValue());}]>;
def u3_1ImmOperand : AsmOperandClass { let Name = "u3_1Imm"; let RenderMethod = "addImmOperands"; }
def u3_1Imm : Operand<i32> { let ParserMatchClass = u3_1ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u3_1ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<3, 1>(N->getSExtValue());}]>;
def u1_0ImmOperand : AsmOperandClass { let Name = "u1_0Imm"; let RenderMethod = "addImmOperands"; }
def u1_0Imm : Operand<i32> { let ParserMatchClass = u1_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u1_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<1, 0>(N->getSExtValue());}]>;
def s31_1ImmOperand : AsmOperandClass { let Name = "s31_1Imm"; let RenderMethod = "addSignedImmOperands"; }
def s31_1Imm : Operand<i32> { let ParserMatchClass = s31_1ImmOperand; let DecoderMethod = "s31_1ImmDecoder"; }
def s31_1ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<32, 1>(N->getSExtValue());}]>;
def s3_0ImmOperand : AsmOperandClass { let Name = "s3_0Imm"; let RenderMethod = "addSignedImmOperands"; }
def s3_0Imm : Operand<i32> { let ParserMatchClass = s3_0ImmOperand; let DecoderMethod = "s3_0ImmDecoder"; }
def s3_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<3, 0>(N->getSExtValue());}]>;
def s30_2ImmOperand : AsmOperandClass { let Name = "s30_2Imm"; let RenderMethod = "addSignedImmOperands"; }
def s30_2Imm : Operand<i32> { let ParserMatchClass = s30_2ImmOperand; let DecoderMethod = "s30_2ImmDecoder"; }
def s30_2ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<32, 2>(N->getSExtValue());}]>;
def u4_0ImmOperand : AsmOperandClass { let Name = "u4_0Imm"; let RenderMethod = "addImmOperands"; }
def u4_0Imm : Operand<i32> { let ParserMatchClass = u4_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u4_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<4, 0>(N->getSExtValue());}]>;
def s6_0ImmOperand : AsmOperandClass { let Name = "s6_0Imm"; let RenderMethod = "addSignedImmOperands"; }
def s6_0Imm : Operand<i32> { let ParserMatchClass = s6_0ImmOperand; let DecoderMethod = "s6_0ImmDecoder"; }
def s6_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<6, 0>(N->getSExtValue());}]>;
def u5_3ImmOperand : AsmOperandClass { let Name = "u5_3Imm"; let RenderMethod = "addImmOperands"; }
def u5_3Imm : Operand<i32> { let ParserMatchClass = u5_3ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u5_3ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<5, 3>(N->getSExtValue());}]>;
def s32_0ImmOperand : AsmOperandClass { let Name = "s32_0Imm"; let RenderMethod = "addSignedImmOperands"; }
def s32_0Imm : Operand<i32> { let ParserMatchClass = s32_0ImmOperand; let DecoderMethod = "s32_0ImmDecoder"; }
def s32_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<32, 0>(N->getSExtValue());}]>;
def s6_3ImmOperand : AsmOperandClass { let Name = "s6_3Imm"; let RenderMethod = "addSignedImmOperands"; }
def s6_3Imm : Operand<i32> { let ParserMatchClass = s6_3ImmOperand; let DecoderMethod = "s6_3ImmDecoder"; }
def s6_3ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<6, 3>(N->getSExtValue());}]>;
def u10_0ImmOperand : AsmOperandClass { let Name = "u10_0Imm"; let RenderMethod = "addImmOperands"; }
def u10_0Imm : Operand<i32> { let ParserMatchClass = u10_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u10_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<10, 0>(N->getSExtValue());}]>;
def u31_1ImmOperand : AsmOperandClass { let Name = "u31_1Imm"; let RenderMethod = "addImmOperands"; }
def u31_1Imm : Operand<i32> { let ParserMatchClass = u31_1ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u31_1ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<32, 1>(N->getSExtValue());}]>;
def s4_1ImmOperand : AsmOperandClass { let Name = "s4_1Imm"; let RenderMethod = "addSignedImmOperands"; }
def s4_1Imm : Operand<i32> { let ParserMatchClass = s4_1ImmOperand; let DecoderMethod = "s4_1ImmDecoder"; }
def s4_1ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<4, 1>(N->getSExtValue());}]>;
def u16_0ImmOperand : AsmOperandClass { let Name = "u16_0Imm"; let RenderMethod = "addImmOperands"; }
def u16_0Imm : Operand<i32> { let ParserMatchClass = u16_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u16_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<16, 0>(N->getSExtValue());}]>;
def u6_1ImmOperand : AsmOperandClass { let Name = "u6_1Imm"; let RenderMethod = "addImmOperands"; }
def u6_1Imm : Operand<i32> { let ParserMatchClass = u6_1ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u6_1ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<6, 1>(N->getSExtValue());}]>;
def u5_2ImmOperand : AsmOperandClass { let Name = "u5_2Imm"; let RenderMethod = "addImmOperands"; }
def u5_2Imm : Operand<i32> { let ParserMatchClass = u5_2ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u5_2ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<5, 2>(N->getSExtValue());}]>;
def u26_6ImmOperand : AsmOperandClass { let Name = "u26_6Imm"; let RenderMethod = "addImmOperands"; }
def u26_6Imm : Operand<i32> { let ParserMatchClass = u26_6ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u26_6ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<26, 6>(N->getSExtValue());}]>;
def u6_2ImmOperand : AsmOperandClass { let Name = "u6_2Imm"; let RenderMethod = "addImmOperands"; }
def u6_2Imm : Operand<i32> { let ParserMatchClass = u6_2ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u6_2ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<6, 2>(N->getSExtValue());}]>;
def u7_0ImmOperand : AsmOperandClass { let Name = "u7_0Imm"; let RenderMethod = "addImmOperands"; }
def u7_0Imm : Operand<i32> { let ParserMatchClass = u7_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u7_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<7, 0>(N->getSExtValue());}]>;
def b13_2ImmOperand : AsmOperandClass { let Name = "b13_2Imm"; let RenderMethod = "addSignedImmOperands"; }
def b13_2Imm : Operand<OtherVT> { let ParserMatchClass = b13_2ImmOperand; let DecoderMethod = "brtargetDecoder"; let PrintMethod = "printBrtarget"; }
def b13_2ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<13, 2>(N->getSExtValue());}]>;
def u5_0ImmOperand : AsmOperandClass { let Name = "u5_0Imm"; let RenderMethod = "addImmOperands"; }
def u5_0Imm : Operand<i32> { let ParserMatchClass = u5_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u5_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<5, 0>(N->getSExtValue());}]>;
def u2_0ImmOperand : AsmOperandClass { let Name = "u2_0Imm"; let RenderMethod = "addImmOperands"; }
def u2_0Imm : Operand<i32> { let ParserMatchClass = u2_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u2_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<2, 0>(N->getSExtValue());}]>;
def s4_2ImmOperand : AsmOperandClass { let Name = "s4_2Imm"; let RenderMethod = "addSignedImmOperands"; }
def s4_2Imm : Operand<i32> { let ParserMatchClass = s4_2ImmOperand; let DecoderMethod = "s4_2ImmDecoder"; }
def s4_2ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<4, 2>(N->getSExtValue());}]>;
def b30_2ImmOperand : AsmOperandClass { let Name = "b30_2Imm"; let RenderMethod = "addSignedImmOperands"; }
def b30_2Imm : Operand<OtherVT> { let ParserMatchClass = b30_2ImmOperand; let DecoderMethod = "brtargetDecoder"; let PrintMethod = "printBrtarget"; }
def b30_2ImmPred : PatLeaf<(i32 imm), [{ return isShiftedInt<32, 2>(N->getSExtValue());}]>;
def u8_0ImmOperand : AsmOperandClass { let Name = "u8_0Imm"; let RenderMethod = "addImmOperands"; }
def u8_0Imm : Operand<i32> { let ParserMatchClass = u8_0ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u8_0ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<8, 0>(N->getSExtValue());}]>;
def u30_2ImmOperand : AsmOperandClass { let Name = "u30_2Imm"; let RenderMethod = "addImmOperands"; }
def u30_2Imm : Operand<i32> { let ParserMatchClass = u30_2ImmOperand; let DecoderMethod = "unsignedImmDecoder"; }
def u30_2ImmPred : PatLeaf<(i32 imm), [{ return isShiftedUInt<32, 2>(N->getSExtValue());}]>;
