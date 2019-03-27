//===- AArch64InstrFormats.td - AArch64 Instruction Formats --*- tblgen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe AArch64 instructions format here
//

// Format specifies the encoding used by the instruction.  This is part of the
// ad-hoc solution used to emit machine instruction encodings by our machine
// code emitter.
class Format<bits<2> val> {
  bits<2> Value = val;
}

def PseudoFrm   : Format<0>;
def NormalFrm   : Format<1>; // Do we need any others?

// AArch64 Instruction Format
class AArch64Inst<Format f, string cstr> : Instruction {
  field bits<32> Inst; // Instruction encoding.
  // Mask of bits that cause an encoding to be UNPREDICTABLE.
  // If a bit is set, then if the corresponding bit in the
  // target encoding differs from its value in the "Inst" field,
  // the instruction is UNPREDICTABLE (SoftFail in abstract parlance).
  field bits<32> Unpredictable = 0;
  // SoftFail is the generic name for this field, but we alias it so
  // as to make it more obvious what it means in ARM-land.
  field bits<32> SoftFail = Unpredictable;
  let Namespace   = "AArch64";
  Format F        = f;
  bits<2> Form    = F.Value;
  let Pattern     = [];
  let Constraints = cstr;
}

class InstSubst<string Asm, dag Result, bit EmitPriority = 0>
  : InstAlias<Asm, Result, EmitPriority>, Requires<[UseNegativeImmediates]>;

// Pseudo instructions (don't have encoding information)
class Pseudo<dag oops, dag iops, list<dag> pattern, string cstr = "">
    : AArch64Inst<PseudoFrm, cstr> {
  dag OutOperandList = oops;
  dag InOperandList  = iops;
  let Pattern        = pattern;
  let isCodeGenOnly  = 1;
}

// Real instructions (have encoding information)
class EncodedI<string cstr, list<dag> pattern> : AArch64Inst<NormalFrm, cstr> {
  let Pattern = pattern;
  let Size = 4;
}

// Enum describing whether an instruction is
// destructive in its first source operand.
class DestructiveInstTypeEnum<bits<1> val> {
  bits<1> Value = val;
}
def NotDestructive  : DestructiveInstTypeEnum<0>;
def Destructive     : DestructiveInstTypeEnum<1>;

// Normal instructions
class I<dag oops, dag iops, string asm, string operands, string cstr,
        list<dag> pattern>
    : EncodedI<cstr, pattern> {
  dag OutOperandList = oops;
  dag InOperandList  = iops;
  let AsmString      = !strconcat(asm, operands);

  // Destructive operations (SVE)
  DestructiveInstTypeEnum DestructiveInstType = NotDestructive;
  ElementSizeEnum ElementSize = ElementSizeB;

  let TSFlags{3} = DestructiveInstType.Value;
  let TSFlags{2-0} = ElementSize.Value;
}

class TriOpFrag<dag res> : PatFrag<(ops node:$LHS, node:$MHS, node:$RHS), res>;
class BinOpFrag<dag res> : PatFrag<(ops node:$LHS, node:$RHS), res>;
class UnOpFrag<dag res>  : PatFrag<(ops node:$LHS), res>;

// Helper fragment for an extract of the high portion of a 128-bit vector.
def extract_high_v16i8 :
   UnOpFrag<(extract_subvector (v16i8 node:$LHS), (i64 8))>;
def extract_high_v8i16 :
   UnOpFrag<(extract_subvector (v8i16 node:$LHS), (i64 4))>;
def extract_high_v4i32 :
   UnOpFrag<(extract_subvector (v4i32 node:$LHS), (i64 2))>;
def extract_high_v2i64 :
   UnOpFrag<(extract_subvector (v2i64 node:$LHS), (i64 1))>;

//===----------------------------------------------------------------------===//
// Asm Operand Classes.
//

// Shifter operand for arithmetic shifted encodings.
def ShifterOperand : AsmOperandClass {
  let Name = "Shifter";
}

// Shifter operand for mov immediate encodings.
def MovImm32ShifterOperand : AsmOperandClass {
  let SuperClasses = [ShifterOperand];
  let Name = "MovImm32Shifter";
  let RenderMethod = "addShifterOperands";
  let DiagnosticType = "InvalidMovImm32Shift";
}
def MovImm64ShifterOperand : AsmOperandClass {
  let SuperClasses = [ShifterOperand];
  let Name = "MovImm64Shifter";
  let RenderMethod = "addShifterOperands";
  let DiagnosticType = "InvalidMovImm64Shift";
}

// Shifter operand for arithmetic register shifted encodings.
class ArithmeticShifterOperand<int width> : AsmOperandClass {
  let SuperClasses = [ShifterOperand];
  let Name = "ArithmeticShifter" # width;
  let PredicateMethod = "isArithmeticShifter<" # width # ">";
  let RenderMethod = "addShifterOperands";
  let DiagnosticType = "AddSubRegShift" # width;
}

def ArithmeticShifterOperand32 : ArithmeticShifterOperand<32>;
def ArithmeticShifterOperand64 : ArithmeticShifterOperand<64>;

// Shifter operand for logical register shifted encodings.
class LogicalShifterOperand<int width> : AsmOperandClass {
  let SuperClasses = [ShifterOperand];
  let Name = "LogicalShifter" # width;
  let PredicateMethod = "isLogicalShifter<" # width # ">";
  let RenderMethod = "addShifterOperands";
  let DiagnosticType = "AddSubRegShift" # width;
}

def LogicalShifterOperand32 : LogicalShifterOperand<32>;
def LogicalShifterOperand64 : LogicalShifterOperand<64>;

// Shifter operand for logical vector 128/64-bit shifted encodings.
def LogicalVecShifterOperand : AsmOperandClass {
  let SuperClasses = [ShifterOperand];
  let Name = "LogicalVecShifter";
  let RenderMethod = "addShifterOperands";
}
def LogicalVecHalfWordShifterOperand : AsmOperandClass {
  let SuperClasses = [LogicalVecShifterOperand];
  let Name = "LogicalVecHalfWordShifter";
  let RenderMethod = "addShifterOperands";
}

// The "MSL" shifter on the vector MOVI instruction.
def MoveVecShifterOperand : AsmOperandClass {
  let SuperClasses = [ShifterOperand];
  let Name = "MoveVecShifter";
  let RenderMethod = "addShifterOperands";
}

// Extend operand for arithmetic encodings.
def ExtendOperand : AsmOperandClass {
  let Name = "Extend";
  let DiagnosticType = "AddSubRegExtendLarge";
}
def ExtendOperand64 : AsmOperandClass {
  let SuperClasses = [ExtendOperand];
  let Name = "Extend64";
  let DiagnosticType = "AddSubRegExtendSmall";
}
// 'extend' that's a lsl of a 64-bit register.
def ExtendOperandLSL64 : AsmOperandClass {
  let SuperClasses = [ExtendOperand];
  let Name = "ExtendLSL64";
  let RenderMethod = "addExtend64Operands";
  let DiagnosticType = "AddSubRegExtendLarge";
}

// 8-bit floating-point immediate encodings.
def FPImmOperand : AsmOperandClass {
  let Name = "FPImm";
  let ParserMethod = "tryParseFPImm<true>";
  let DiagnosticType = "InvalidFPImm";
}

def CondCode : AsmOperandClass {
  let Name = "CondCode";
  let DiagnosticType = "InvalidCondCode";
}

// A 32-bit register pasrsed as 64-bit
def GPR32as64Operand : AsmOperandClass {
  let Name = "GPR32as64";
  let ParserMethod =
      "tryParseGPROperand<false, RegConstraintEqualityTy::EqualsSubReg>";
}
def GPR32as64 : RegisterOperand<GPR32> {
  let ParserMatchClass = GPR32as64Operand;
}

// A 64-bit register pasrsed as 32-bit
def GPR64as32Operand : AsmOperandClass {
  let Name = "GPR64as32";
  let ParserMethod =
      "tryParseGPROperand<false, RegConstraintEqualityTy::EqualsSuperReg>";
}
def GPR64as32 : RegisterOperand<GPR64, "printGPR64as32"> {
  let ParserMatchClass = GPR64as32Operand;
}

// 8-bit immediate for AdvSIMD where 64-bit values of the form:
// aaaaaaaa bbbbbbbb cccccccc dddddddd eeeeeeee ffffffff gggggggg hhhhhhhh
// are encoded as the eight bit value 'abcdefgh'.
def SIMDImmType10Operand : AsmOperandClass { let Name = "SIMDImmType10"; }

class UImmScaledMemoryIndexed<int Width, int Scale> : AsmOperandClass {
  let Name = "UImm" # Width # "s" # Scale;
  let DiagnosticType = "InvalidMemoryIndexed" # Scale # "UImm" # Width;
  let RenderMethod = "addImmScaledOperands<" # Scale # ">";
  let PredicateMethod = "isUImmScaled<" # Width # ", " # Scale # ">";
}

class SImmScaledMemoryIndexed<int Width, int Scale> : AsmOperandClass {
  let Name = "SImm" # Width # "s" # Scale;
  let DiagnosticType = "InvalidMemoryIndexed" # Scale # "SImm" # Width;
  let RenderMethod = "addImmScaledOperands<" # Scale # ">";
  let PredicateMethod = "isSImmScaled<" # Width # ", " # Scale # ">";
}

//===----------------------------------------------------------------------===//
// Operand Definitions.
//

// ADR[P] instruction labels.
def AdrpOperand : AsmOperandClass {
  let Name = "AdrpLabel";
  let ParserMethod = "tryParseAdrpLabel";
  let DiagnosticType = "InvalidLabel";
}
def adrplabel : Operand<i64> {
  let EncoderMethod = "getAdrLabelOpValue";
  let PrintMethod = "printAdrpLabel";
  let ParserMatchClass = AdrpOperand;
}

def AdrOperand : AsmOperandClass {
  let Name = "AdrLabel";
  let ParserMethod = "tryParseAdrLabel";
  let DiagnosticType = "InvalidLabel";
}
def adrlabel : Operand<i64> {
  let EncoderMethod = "getAdrLabelOpValue";
  let ParserMatchClass = AdrOperand;
}

class SImmOperand<int width> : AsmOperandClass {
  let Name = "SImm" # width;
  let DiagnosticType = "InvalidMemoryIndexedSImm" # width;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isSImm<" # width # ">";
}


class AsmImmRange<int Low, int High> : AsmOperandClass {
  let Name = "Imm" # Low # "_" # High;
  let DiagnosticType = "InvalidImm" # Low # "_" # High;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isImmInRange<" # Low # "," # High # ">";
}

// Authenticated loads for v8.3 can have scaled 10-bit immediate offsets.
def SImm10s8Operand : SImmScaledMemoryIndexed<10, 8>;
def simm10Scaled : Operand<i64> {
  let ParserMatchClass = SImm10s8Operand;
  let DecoderMethod = "DecodeSImm<10>";
  let PrintMethod = "printImmScale<8>";
}

def simm9s16 : Operand<i64> {
  let ParserMatchClass = SImmScaledMemoryIndexed<9, 16>;
  let DecoderMethod = "DecodeSImm<9>";
  let PrintMethod = "printImmScale<16>";
}

// uimm6 predicate - True if the immediate is in the range [0, 63].
def UImm6Operand : AsmOperandClass {
  let Name = "UImm6";
  let DiagnosticType = "InvalidImm0_63";
}

def uimm6 : Operand<i64>, ImmLeaf<i64, [{ return Imm >= 0 && Imm < 64; }]> {
  let ParserMatchClass = UImm6Operand;
}

def uimm16 : Operand<i16>, ImmLeaf<i16, [{return Imm >= 0 && Imm < 65536;}]>{
  let ParserMatchClass = AsmImmRange<0, 65535>;
}

def SImm9Operand : SImmOperand<9>;
def simm9 : Operand<i64>, ImmLeaf<i64, [{ return Imm >= -256 && Imm < 256; }]> {
  let ParserMatchClass = SImm9Operand;
  let DecoderMethod = "DecodeSImm<9>";
}

def SImm8Operand : SImmOperand<8>;
def simm8 : Operand<i64>, ImmLeaf<i64, [{ return Imm >= -128 && Imm < 127; }]> {
  let ParserMatchClass = SImm8Operand;
  let DecoderMethod = "DecodeSImm<8>";
}

def SImm6Operand : SImmOperand<6>;
def simm6_32b : Operand<i32>, ImmLeaf<i32, [{ return Imm >= -32 && Imm < 32; }]> {
  let ParserMatchClass = SImm6Operand;
  let DecoderMethod = "DecodeSImm<6>";
}

def SImm5Operand : SImmOperand<5>;
def simm5_64b : Operand<i64>, ImmLeaf<i64, [{ return Imm >= -16 && Imm < 16; }]> {
  let ParserMatchClass = SImm5Operand;
  let DecoderMethod = "DecodeSImm<5>";
}

def simm5_32b : Operand<i32>, ImmLeaf<i32, [{ return Imm >= -16 && Imm < 16; }]> {
  let ParserMatchClass = SImm5Operand;
  let DecoderMethod = "DecodeSImm<5>";
}

// simm7sN predicate - True if the immediate is a multiple of N in the range
// [-64 * N, 63 * N].

def SImm7s4Operand : SImmScaledMemoryIndexed<7, 4>;
def SImm7s8Operand : SImmScaledMemoryIndexed<7, 8>;
def SImm7s16Operand : SImmScaledMemoryIndexed<7, 16>;

def simm7s4 : Operand<i32> {
  let ParserMatchClass = SImm7s4Operand;
  let PrintMethod = "printImmScale<4>";
}

def simm7s8 : Operand<i32> {
  let ParserMatchClass = SImm7s8Operand;
  let PrintMethod = "printImmScale<8>";
}

def simm7s16 : Operand<i32> {
  let ParserMatchClass = SImm7s16Operand;
  let PrintMethod = "printImmScale<16>";
}

def am_indexed7s8   : ComplexPattern<i64, 2, "SelectAddrModeIndexed7S8", []>;
def am_indexed7s16  : ComplexPattern<i64, 2, "SelectAddrModeIndexed7S16", []>;
def am_indexed7s32  : ComplexPattern<i64, 2, "SelectAddrModeIndexed7S32", []>;
def am_indexed7s64  : ComplexPattern<i64, 2, "SelectAddrModeIndexed7S64", []>;
def am_indexed7s128 : ComplexPattern<i64, 2, "SelectAddrModeIndexed7S128", []>;

// uimm5sN predicate - True if the immediate is a multiple of N in the range
// [0 * N, 32 * N].
def UImm5s2Operand : UImmScaledMemoryIndexed<5, 2>;
def UImm5s4Operand : UImmScaledMemoryIndexed<5, 4>;
def UImm5s8Operand : UImmScaledMemoryIndexed<5, 8>;

def uimm5s2 : Operand<i64>, ImmLeaf<i64,
                [{ return Imm >= 0 && Imm < (32*2) && ((Imm % 2) == 0); }]> {
  let ParserMatchClass = UImm5s2Operand;
  let PrintMethod = "printImmScale<2>";
}
def uimm5s4 : Operand<i64>, ImmLeaf<i64,
                [{ return Imm >= 0 && Imm < (32*4) && ((Imm % 4) == 0); }]> {
  let ParserMatchClass = UImm5s4Operand;
  let PrintMethod = "printImmScale<4>";
}
def uimm5s8 : Operand<i64>, ImmLeaf<i64,
                [{ return Imm >= 0 && Imm < (32*8) && ((Imm % 8) == 0); }]> {
  let ParserMatchClass = UImm5s8Operand;
  let PrintMethod = "printImmScale<8>";
}

// uimm6sN predicate - True if the immediate is a multiple of N in the range
// [0 * N, 64 * N].
def UImm6s1Operand : UImmScaledMemoryIndexed<6, 1>;
def UImm6s2Operand : UImmScaledMemoryIndexed<6, 2>;
def UImm6s4Operand : UImmScaledMemoryIndexed<6, 4>;
def UImm6s8Operand : UImmScaledMemoryIndexed<6, 8>;
def UImm6s16Operand : UImmScaledMemoryIndexed<6, 16>;

def uimm6s1 : Operand<i64>, ImmLeaf<i64, [{ return Imm >= 0 && Imm < 64; }]> {
  let ParserMatchClass = UImm6s1Operand;
}
def uimm6s2 : Operand<i64>, ImmLeaf<i64,
[{ return Imm >= 0 && Imm < (64*2) && ((Imm % 2) == 0); }]> {
  let PrintMethod = "printImmScale<2>";
  let ParserMatchClass = UImm6s2Operand;
}
def uimm6s4 : Operand<i64>, ImmLeaf<i64,
[{ return Imm >= 0 && Imm < (64*4) && ((Imm % 4) == 0); }]> {
  let PrintMethod = "printImmScale<4>";
  let ParserMatchClass = UImm6s4Operand;
}
def uimm6s8 : Operand<i64>, ImmLeaf<i64,
[{ return Imm >= 0 && Imm < (64*8) && ((Imm % 8) == 0); }]> {
  let PrintMethod = "printImmScale<8>";
  let ParserMatchClass = UImm6s8Operand;
}
def uimm6s16 : Operand<i64>, ImmLeaf<i64,
[{ return Imm >= 0 && Imm < (64*16) && ((Imm % 16) == 0); }]> {
  let PrintMethod = "printImmScale<16>";
  let ParserMatchClass = UImm6s16Operand;
}

// simm6sN predicate - True if the immediate is a multiple of N in the range
// [-32 * N, 31 * N].
def SImm6s1Operand : SImmScaledMemoryIndexed<6, 1>;
def simm6s1 : Operand<i64>, ImmLeaf<i64, [{ return Imm >= -32 && Imm < 32; }]> {
  let ParserMatchClass = SImm6s1Operand;
  let DecoderMethod = "DecodeSImm<6>";
}

// simm4sN predicate - True if the immediate is a multiple of N in the range
// [ -8* N, 7 * N].
def SImm4s1Operand  : SImmScaledMemoryIndexed<4, 1>;
def SImm4s2Operand  : SImmScaledMemoryIndexed<4, 2>;
def SImm4s3Operand  : SImmScaledMemoryIndexed<4, 3>;
def SImm4s4Operand  : SImmScaledMemoryIndexed<4, 4>;
def SImm4s16Operand : SImmScaledMemoryIndexed<4, 16>;

def simm4s1 : Operand<i64>, ImmLeaf<i64,
[{ return Imm >=-8  && Imm <= 7; }]> {
  let ParserMatchClass = SImm4s1Operand;
  let DecoderMethod = "DecodeSImm<4>";
}

def simm4s2 : Operand<i64>, ImmLeaf<i64,
[{ return Imm >=-16  && Imm <= 14 && (Imm % 2) == 0x0; }]> {
  let PrintMethod = "printImmScale<2>";
  let ParserMatchClass = SImm4s2Operand;
  let DecoderMethod = "DecodeSImm<4>";
}

def simm4s3 : Operand<i64>, ImmLeaf<i64,
[{ return Imm >=-24  && Imm <= 21 && (Imm % 3) == 0x0; }]> {
  let PrintMethod = "printImmScale<3>";
  let ParserMatchClass = SImm4s3Operand;
  let DecoderMethod = "DecodeSImm<4>";
}

def simm4s4 : Operand<i64>, ImmLeaf<i64,
[{ return Imm >=-32  && Imm <= 28 && (Imm % 4) == 0x0; }]> {
  let PrintMethod = "printImmScale<4>";
  let ParserMatchClass = SImm4s4Operand;
  let DecoderMethod = "DecodeSImm<4>";
}
def simm4s16 : Operand<i64>, ImmLeaf<i64,
[{ return Imm >=-128  && Imm <= 112 && (Imm % 16) == 0x0; }]> {
  let PrintMethod = "printImmScale<16>";
  let ParserMatchClass = SImm4s16Operand;
  let DecoderMethod = "DecodeSImm<4>";
}

def Imm1_8Operand : AsmImmRange<1, 8>;
def Imm1_16Operand : AsmImmRange<1, 16>;
def Imm1_32Operand : AsmImmRange<1, 32>;
def Imm1_64Operand : AsmImmRange<1, 64>;

class BranchTarget<int N> : AsmOperandClass {
  let Name = "BranchTarget" # N;
  let DiagnosticType = "InvalidLabel";
  let PredicateMethod = "isBranchTarget<" # N # ">";
}

class PCRelLabel<int N> : BranchTarget<N> {
  let Name = "PCRelLabel" # N;
}

def BranchTarget14Operand : BranchTarget<14>;
def BranchTarget26Operand : BranchTarget<26>;
def PCRelLabel19Operand   : PCRelLabel<19>;

def MovZSymbolG3AsmOperand : AsmOperandClass {
  let Name = "MovZSymbolG3";
  let RenderMethod = "addImmOperands";
}

def movz_symbol_g3 : Operand<i32> {
  let ParserMatchClass = MovZSymbolG3AsmOperand;
}

def MovZSymbolG2AsmOperand : AsmOperandClass {
  let Name = "MovZSymbolG2";
  let RenderMethod = "addImmOperands";
}

def movz_symbol_g2 : Operand<i32> {
  let ParserMatchClass = MovZSymbolG2AsmOperand;
}

def MovZSymbolG1AsmOperand : AsmOperandClass {
  let Name = "MovZSymbolG1";
  let RenderMethod = "addImmOperands";
}

def movz_symbol_g1 : Operand<i32> {
  let ParserMatchClass = MovZSymbolG1AsmOperand;
}

def MovZSymbolG0AsmOperand : AsmOperandClass {
  let Name = "MovZSymbolG0";
  let RenderMethod = "addImmOperands";
}

def movz_symbol_g0 : Operand<i32> {
  let ParserMatchClass = MovZSymbolG0AsmOperand;
}

def MovKSymbolG3AsmOperand : AsmOperandClass {
  let Name = "MovKSymbolG3";
  let RenderMethod = "addImmOperands";
}

def movk_symbol_g3 : Operand<i32> {
  let ParserMatchClass = MovKSymbolG3AsmOperand;
}

def MovKSymbolG2AsmOperand : AsmOperandClass {
  let Name = "MovKSymbolG2";
  let RenderMethod = "addImmOperands";
}

def movk_symbol_g2 : Operand<i32> {
  let ParserMatchClass = MovKSymbolG2AsmOperand;
}

def MovKSymbolG1AsmOperand : AsmOperandClass {
  let Name = "MovKSymbolG1";
  let RenderMethod = "addImmOperands";
}

def movk_symbol_g1 : Operand<i32> {
  let ParserMatchClass = MovKSymbolG1AsmOperand;
}

def MovKSymbolG0AsmOperand : AsmOperandClass {
  let Name = "MovKSymbolG0";
  let RenderMethod = "addImmOperands";
}

def movk_symbol_g0 : Operand<i32> {
  let ParserMatchClass = MovKSymbolG0AsmOperand;
}

class fixedpoint_i32<ValueType FloatVT>
  : Operand<FloatVT>,
    ComplexPattern<FloatVT, 1, "SelectCVTFixedPosOperand<32>", [fpimm, ld]> {
  let EncoderMethod = "getFixedPointScaleOpValue";
  let DecoderMethod = "DecodeFixedPointScaleImm32";
  let ParserMatchClass = Imm1_32Operand;
}

class fixedpoint_i64<ValueType FloatVT>
  : Operand<FloatVT>,
    ComplexPattern<FloatVT, 1, "SelectCVTFixedPosOperand<64>", [fpimm, ld]> {
  let EncoderMethod = "getFixedPointScaleOpValue";
  let DecoderMethod = "DecodeFixedPointScaleImm64";
  let ParserMatchClass = Imm1_64Operand;
}

def fixedpoint_f16_i32 : fixedpoint_i32<f16>;
def fixedpoint_f32_i32 : fixedpoint_i32<f32>;
def fixedpoint_f64_i32 : fixedpoint_i32<f64>;

def fixedpoint_f16_i64 : fixedpoint_i64<f16>;
def fixedpoint_f32_i64 : fixedpoint_i64<f32>;
def fixedpoint_f64_i64 : fixedpoint_i64<f64>;

def vecshiftR8 : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) > 0) && (((uint32_t)Imm) < 9);
}]> {
  let EncoderMethod = "getVecShiftR8OpValue";
  let DecoderMethod = "DecodeVecShiftR8Imm";
  let ParserMatchClass = Imm1_8Operand;
}
def vecshiftR16 : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) > 0) && (((uint32_t)Imm) < 17);
}]> {
  let EncoderMethod = "getVecShiftR16OpValue";
  let DecoderMethod = "DecodeVecShiftR16Imm";
  let ParserMatchClass = Imm1_16Operand;
}
def vecshiftR16Narrow : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) > 0) && (((uint32_t)Imm) < 9);
}]> {
  let EncoderMethod = "getVecShiftR16OpValue";
  let DecoderMethod = "DecodeVecShiftR16ImmNarrow";
  let ParserMatchClass = Imm1_8Operand;
}
def vecshiftR32 : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) > 0) && (((uint32_t)Imm) < 33);
}]> {
  let EncoderMethod = "getVecShiftR32OpValue";
  let DecoderMethod = "DecodeVecShiftR32Imm";
  let ParserMatchClass = Imm1_32Operand;
}
def vecshiftR32Narrow : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) > 0) && (((uint32_t)Imm) < 17);
}]> {
  let EncoderMethod = "getVecShiftR32OpValue";
  let DecoderMethod = "DecodeVecShiftR32ImmNarrow";
  let ParserMatchClass = Imm1_16Operand;
}
def vecshiftR64 : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) > 0) && (((uint32_t)Imm) < 65);
}]> {
  let EncoderMethod = "getVecShiftR64OpValue";
  let DecoderMethod = "DecodeVecShiftR64Imm";
  let ParserMatchClass = Imm1_64Operand;
}
def vecshiftR64Narrow : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) > 0) && (((uint32_t)Imm) < 33);
}]> {
  let EncoderMethod = "getVecShiftR64OpValue";
  let DecoderMethod = "DecodeVecShiftR64ImmNarrow";
  let ParserMatchClass = Imm1_32Operand;
}

def Imm0_1Operand : AsmImmRange<0, 1>;
def Imm0_7Operand : AsmImmRange<0, 7>;
def Imm0_15Operand : AsmImmRange<0, 15>;
def Imm0_31Operand : AsmImmRange<0, 31>;
def Imm0_63Operand : AsmImmRange<0, 63>;

def vecshiftL8 : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) < 8);
}]> {
  let EncoderMethod = "getVecShiftL8OpValue";
  let DecoderMethod = "DecodeVecShiftL8Imm";
  let ParserMatchClass = Imm0_7Operand;
}
def vecshiftL16 : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) < 16);
}]> {
  let EncoderMethod = "getVecShiftL16OpValue";
  let DecoderMethod = "DecodeVecShiftL16Imm";
  let ParserMatchClass = Imm0_15Operand;
}
def vecshiftL32 : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) < 32);
}]> {
  let EncoderMethod = "getVecShiftL32OpValue";
  let DecoderMethod = "DecodeVecShiftL32Imm";
  let ParserMatchClass = Imm0_31Operand;
}
def vecshiftL64 : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) < 64);
}]> {
  let EncoderMethod = "getVecShiftL64OpValue";
  let DecoderMethod = "DecodeVecShiftL64Imm";
  let ParserMatchClass = Imm0_63Operand;
}


// Crazy immediate formats used by 32-bit and 64-bit logical immediate
// instructions for splatting repeating bit patterns across the immediate.
def logical_imm32_XFORM : SDNodeXForm<imm, [{
  uint64_t enc = AArch64_AM::encodeLogicalImmediate(N->getZExtValue(), 32);
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i32);
}]>;
def logical_imm64_XFORM : SDNodeXForm<imm, [{
  uint64_t enc = AArch64_AM::encodeLogicalImmediate(N->getZExtValue(), 64);
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i32);
}]>;

let DiagnosticType = "LogicalSecondSource" in {
  def LogicalImm32Operand : AsmOperandClass {
    let Name = "LogicalImm32";
    let PredicateMethod = "isLogicalImm<int32_t>";
    let RenderMethod = "addLogicalImmOperands<int32_t>";
  }
  def LogicalImm64Operand : AsmOperandClass {
    let Name = "LogicalImm64";
    let PredicateMethod = "isLogicalImm<int64_t>";
    let RenderMethod = "addLogicalImmOperands<int64_t>";
  }
  def LogicalImm32NotOperand : AsmOperandClass {
    let Name = "LogicalImm32Not";
    let PredicateMethod = "isLogicalImm<int32_t>";
    let RenderMethod = "addLogicalImmNotOperands<int32_t>";
  }
  def LogicalImm64NotOperand : AsmOperandClass {
    let Name = "LogicalImm64Not";
    let PredicateMethod = "isLogicalImm<int64_t>";
    let RenderMethod = "addLogicalImmNotOperands<int64_t>";
  }
}
def logical_imm32 : Operand<i32>, IntImmLeaf<i32, [{
  return AArch64_AM::isLogicalImmediate(Imm.getZExtValue(), 32);
}], logical_imm32_XFORM> {
  let PrintMethod = "printLogicalImm<int32_t>";
  let ParserMatchClass = LogicalImm32Operand;
}
def logical_imm64 : Operand<i64>, IntImmLeaf<i64, [{
  return AArch64_AM::isLogicalImmediate(Imm.getZExtValue(), 64);
}], logical_imm64_XFORM> {
  let PrintMethod = "printLogicalImm<int64_t>";
  let ParserMatchClass = LogicalImm64Operand;
}
def logical_imm32_not : Operand<i32> {
  let ParserMatchClass = LogicalImm32NotOperand;
}
def logical_imm64_not : Operand<i64> {
  let ParserMatchClass = LogicalImm64NotOperand;
}

// imm0_65535 predicate - True if the immediate is in the range [0,65535].
def imm0_65535 : Operand<i32>, ImmLeaf<i32, [{
  return ((uint32_t)Imm) < 65536;
}]> {
  let ParserMatchClass = AsmImmRange<0, 65535>;
  let PrintMethod = "printImmHex";
}

// imm0_255 predicate - True if the immediate is in the range [0,255].
def Imm0_255Operand : AsmImmRange<0,255>;

def imm0_255 : Operand<i32>, ImmLeaf<i32, [{
  return ((uint32_t)Imm) < 256;
}]> {
  let ParserMatchClass = Imm0_255Operand;
  let PrintMethod = "printImm";
}

// imm0_127 predicate - True if the immediate is in the range [0,127]
def Imm0_127Operand : AsmImmRange<0, 127>;
def imm0_127 : Operand<i32>, ImmLeaf<i32, [{
  return ((uint32_t)Imm) < 128;
}]> {
  let ParserMatchClass = Imm0_127Operand;
  let PrintMethod = "printImm";
}

// NOTE: These imm0_N operands have to be of type i64 because i64 is the size
// for all shift-amounts.

// imm0_63 predicate - True if the immediate is in the range [0,63]
def imm0_63 : Operand<i64>, ImmLeaf<i64, [{
  return ((uint64_t)Imm) < 64;
}]> {
  let ParserMatchClass = Imm0_63Operand;
}

// imm0_31 predicate - True if the immediate is in the range [0,31]
def imm0_31 : Operand<i64>, ImmLeaf<i64, [{
  return ((uint64_t)Imm) < 32;
}]> {
  let ParserMatchClass = Imm0_31Operand;
}

// True if the 32-bit immediate is in the range [0,31]
def imm32_0_31 : Operand<i32>, ImmLeaf<i32, [{
  return ((uint64_t)Imm) < 32;
}]> {
  let ParserMatchClass = Imm0_31Operand;
}

// imm0_1 predicate - True if the immediate is in the range [0,1]
def imm0_1 : Operand<i64>, ImmLeaf<i64, [{
  return ((uint64_t)Imm) < 2;
}]> {
  let ParserMatchClass = Imm0_1Operand;
}

// imm0_15 predicate - True if the immediate is in the range [0,15]
def imm0_15 : Operand<i64>, ImmLeaf<i64, [{
  return ((uint64_t)Imm) < 16;
}]> {
  let ParserMatchClass = Imm0_15Operand;
}

// imm0_7 predicate - True if the immediate is in the range [0,7]
def imm0_7 : Operand<i64>, ImmLeaf<i64, [{
  return ((uint64_t)Imm) < 8;
}]> {
  let ParserMatchClass = Imm0_7Operand;
}

// imm32_0_15 predicate - True if the 32-bit immediate is in the range [0,15]
def imm32_0_15 : Operand<i32>, ImmLeaf<i32, [{
  return ((uint32_t)Imm) < 16;
}]> {
  let ParserMatchClass = Imm0_15Operand;
}

// An arithmetic shifter operand:
//  {7-6} - shift type: 00 = lsl, 01 = lsr, 10 = asr
//  {5-0} - imm6
class arith_shift<ValueType Ty, int width> : Operand<Ty> {
  let PrintMethod = "printShifter";
  let ParserMatchClass = !cast<AsmOperandClass>(
                         "ArithmeticShifterOperand" # width);
}

def arith_shift32 : arith_shift<i32, 32>;
def arith_shift64 : arith_shift<i64, 64>;

class arith_shifted_reg<ValueType Ty, RegisterClass regclass, int width>
    : Operand<Ty>,
      ComplexPattern<Ty, 2, "SelectArithShiftedRegister", []> {
  let PrintMethod = "printShiftedRegister";
  let MIOperandInfo = (ops regclass, !cast<Operand>("arith_shift" # width));
}

def arith_shifted_reg32 : arith_shifted_reg<i32, GPR32, 32>;
def arith_shifted_reg64 : arith_shifted_reg<i64, GPR64, 64>;

// An arithmetic shifter operand:
//  {7-6} - shift type: 00 = lsl, 01 = lsr, 10 = asr, 11 = ror
//  {5-0} - imm6
class logical_shift<int width> : Operand<i32> {
  let PrintMethod = "printShifter";
  let ParserMatchClass = !cast<AsmOperandClass>(
                         "LogicalShifterOperand" # width);
}

def logical_shift32 : logical_shift<32>;
def logical_shift64 : logical_shift<64>;

class logical_shifted_reg<ValueType Ty, RegisterClass regclass, Operand shiftop>
    : Operand<Ty>,
      ComplexPattern<Ty, 2, "SelectLogicalShiftedRegister", []> {
  let PrintMethod = "printShiftedRegister";
  let MIOperandInfo = (ops regclass, shiftop);
}

def logical_shifted_reg32 : logical_shifted_reg<i32, GPR32, logical_shift32>;
def logical_shifted_reg64 : logical_shifted_reg<i64, GPR64, logical_shift64>;

// A logical vector shifter operand:
//  {7-6} - shift type: 00 = lsl
//  {5-0} - imm6: #0, #8, #16, or #24
def logical_vec_shift : Operand<i32> {
  let PrintMethod = "printShifter";
  let EncoderMethod = "getVecShifterOpValue";
  let ParserMatchClass = LogicalVecShifterOperand;
}

// A logical vector half-word shifter operand:
//  {7-6} - shift type: 00 = lsl
//  {5-0} - imm6: #0 or #8
def logical_vec_hw_shift : Operand<i32> {
  let PrintMethod = "printShifter";
  let EncoderMethod = "getVecShifterOpValue";
  let ParserMatchClass = LogicalVecHalfWordShifterOperand;
}

// A vector move shifter operand:
//  {0} - imm1: #8 or #16
def move_vec_shift : Operand<i32> {
  let PrintMethod = "printShifter";
  let EncoderMethod = "getMoveVecShifterOpValue";
  let ParserMatchClass = MoveVecShifterOperand;
}

let DiagnosticType = "AddSubSecondSource" in {
  def AddSubImmOperand : AsmOperandClass {
    let Name = "AddSubImm";
    let ParserMethod = "tryParseImmWithOptionalShift";
    let RenderMethod = "addImmWithOptionalShiftOperands<12>";
  }
  def AddSubImmNegOperand : AsmOperandClass {
    let Name = "AddSubImmNeg";
    let ParserMethod = "tryParseImmWithOptionalShift";
    let RenderMethod = "addImmNegWithOptionalShiftOperands<12>";
  }
}
// An ADD/SUB immediate shifter operand:
//  second operand:
//  {7-6} - shift type: 00 = lsl
//  {5-0} - imm6: #0 or #12
class addsub_shifted_imm<ValueType Ty>
    : Operand<Ty>, ComplexPattern<Ty, 2, "SelectArithImmed", [imm]> {
  let PrintMethod = "printAddSubImm";
  let EncoderMethod = "getAddSubImmOpValue";
  let ParserMatchClass = AddSubImmOperand;
  let MIOperandInfo = (ops i32imm, i32imm);
}

class addsub_shifted_imm_neg<ValueType Ty>
    : Operand<Ty> {
  let EncoderMethod = "getAddSubImmOpValue";
  let ParserMatchClass = AddSubImmNegOperand;
  let MIOperandInfo = (ops i32imm, i32imm);
}

def addsub_shifted_imm32 : addsub_shifted_imm<i32>;
def addsub_shifted_imm64 : addsub_shifted_imm<i64>;
def addsub_shifted_imm32_neg : addsub_shifted_imm_neg<i32>;
def addsub_shifted_imm64_neg : addsub_shifted_imm_neg<i64>;

def gi_addsub_shifted_imm32 :
    GIComplexOperandMatcher<s32, "selectArithImmed">,
    GIComplexPatternEquiv<addsub_shifted_imm32>;

def gi_addsub_shifted_imm64 :
    GIComplexOperandMatcher<s64, "selectArithImmed">,
    GIComplexPatternEquiv<addsub_shifted_imm64>;

class neg_addsub_shifted_imm<ValueType Ty>
    : Operand<Ty>, ComplexPattern<Ty, 2, "SelectNegArithImmed", [imm]> {
  let PrintMethod = "printAddSubImm";
  let EncoderMethod = "getAddSubImmOpValue";
  let ParserMatchClass = AddSubImmOperand;
  let MIOperandInfo = (ops i32imm, i32imm);
}

def neg_addsub_shifted_imm32 : neg_addsub_shifted_imm<i32>;
def neg_addsub_shifted_imm64 : neg_addsub_shifted_imm<i64>;

// An extend operand:
//  {5-3} - extend type
//  {2-0} - imm3
def arith_extend : Operand<i32> {
  let PrintMethod = "printArithExtend";
  let ParserMatchClass = ExtendOperand;
}
def arith_extend64 : Operand<i32> {
  let PrintMethod = "printArithExtend";
  let ParserMatchClass = ExtendOperand64;
}

// 'extend' that's a lsl of a 64-bit register.
def arith_extendlsl64 : Operand<i32> {
  let PrintMethod = "printArithExtend";
  let ParserMatchClass = ExtendOperandLSL64;
}

class arith_extended_reg32<ValueType Ty> : Operand<Ty>,
                    ComplexPattern<Ty, 2, "SelectArithExtendedRegister", []> {
  let PrintMethod = "printExtendedRegister";
  let MIOperandInfo = (ops GPR32, arith_extend);
}

class arith_extended_reg32to64<ValueType Ty> : Operand<Ty>,
                    ComplexPattern<Ty, 2, "SelectArithExtendedRegister", []> {
  let PrintMethod = "printExtendedRegister";
  let MIOperandInfo = (ops GPR32, arith_extend64);
}

// Floating-point immediate.
def fpimm16 : Operand<f16>,
              FPImmLeaf<f16, [{
      return AArch64_AM::getFP16Imm(Imm) != -1;
    }], SDNodeXForm<fpimm, [{
      APFloat InVal = N->getValueAPF();
      uint32_t enc = AArch64_AM::getFP16Imm(InVal);
      return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i32);
    }]>> {
  let ParserMatchClass = FPImmOperand;
  let PrintMethod = "printFPImmOperand";
}
def fpimm32 : Operand<f32>,
              FPImmLeaf<f32, [{
      return AArch64_AM::getFP32Imm(Imm) != -1;
    }], SDNodeXForm<fpimm, [{
      APFloat InVal = N->getValueAPF();
      uint32_t enc = AArch64_AM::getFP32Imm(InVal);
      return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i32);
    }]>> {
  let ParserMatchClass = FPImmOperand;
  let PrintMethod = "printFPImmOperand";
}
def fpimm64 : Operand<f64>,
              FPImmLeaf<f64, [{
      return AArch64_AM::getFP64Imm(Imm) != -1;
    }], SDNodeXForm<fpimm, [{
      APFloat InVal = N->getValueAPF();
      uint32_t enc = AArch64_AM::getFP64Imm(InVal);
      return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i32);
    }]>> {
  let ParserMatchClass = FPImmOperand;
  let PrintMethod = "printFPImmOperand";
}

def fpimm8 : Operand<i32> {
  let ParserMatchClass = FPImmOperand;
  let PrintMethod = "printFPImmOperand";
}

def fpimm0 : FPImmLeaf<fAny, [{
  return Imm.isExactlyValue(+0.0);
}]>;

// Vector lane operands
class AsmVectorIndex<int Min, int Max, string NamePrefix=""> : AsmOperandClass {
  let Name = NamePrefix # "IndexRange" # Min # "_" # Max;
  let DiagnosticType = "Invalid" # Name;
  let PredicateMethod = "isVectorIndex<" # Min # ", " # Max #  ">";
  let RenderMethod = "addVectorIndexOperands";
}

class AsmVectorIndexOpnd<AsmOperandClass mc, code pred>
    : Operand<i64>, ImmLeaf<i64, pred> {
  let ParserMatchClass = mc;
  let PrintMethod = "printVectorIndex";
}

def VectorIndex1Operand : AsmVectorIndex<1, 1>;
def VectorIndexBOperand : AsmVectorIndex<0, 15>;
def VectorIndexHOperand : AsmVectorIndex<0, 7>;
def VectorIndexSOperand : AsmVectorIndex<0, 3>;
def VectorIndexDOperand : AsmVectorIndex<0, 1>;

def VectorIndex1 : AsmVectorIndexOpnd<VectorIndex1Operand, [{ return ((uint64_t)Imm) == 1; }]>;
def VectorIndexB : AsmVectorIndexOpnd<VectorIndexBOperand, [{ return ((uint64_t)Imm) < 16; }]>;
def VectorIndexH : AsmVectorIndexOpnd<VectorIndexHOperand, [{ return ((uint64_t)Imm) < 8; }]>;
def VectorIndexS : AsmVectorIndexOpnd<VectorIndexSOperand, [{ return ((uint64_t)Imm) < 4; }]>;
def VectorIndexD : AsmVectorIndexOpnd<VectorIndexDOperand, [{ return ((uint64_t)Imm) < 2; }]>;

def SVEVectorIndexExtDupBOperand : AsmVectorIndex<0, 63, "SVE">;
def SVEVectorIndexExtDupHOperand : AsmVectorIndex<0, 31, "SVE">;
def SVEVectorIndexExtDupSOperand : AsmVectorIndex<0, 15, "SVE">;
def SVEVectorIndexExtDupDOperand : AsmVectorIndex<0, 7, "SVE">;
def SVEVectorIndexExtDupQOperand : AsmVectorIndex<0, 3, "SVE">;

def sve_elm_idx_extdup_b
  : AsmVectorIndexOpnd<SVEVectorIndexExtDupBOperand, [{ return ((uint64_t)Imm) < 64; }]>;
def sve_elm_idx_extdup_h
  : AsmVectorIndexOpnd<SVEVectorIndexExtDupHOperand, [{ return ((uint64_t)Imm) < 32; }]>;
def sve_elm_idx_extdup_s
  : AsmVectorIndexOpnd<SVEVectorIndexExtDupSOperand, [{ return ((uint64_t)Imm) < 16; }]>;
def sve_elm_idx_extdup_d
  : AsmVectorIndexOpnd<SVEVectorIndexExtDupDOperand, [{ return ((uint64_t)Imm) < 8; }]>;
def sve_elm_idx_extdup_q
  : AsmVectorIndexOpnd<SVEVectorIndexExtDupQOperand, [{ return ((uint64_t)Imm) < 4; }]>;

// 8-bit immediate for AdvSIMD where 64-bit values of the form:
// aaaaaaaa bbbbbbbb cccccccc dddddddd eeeeeeee ffffffff gggggggg hhhhhhhh
// are encoded as the eight bit value 'abcdefgh'.
def simdimmtype10 : Operand<i32>,
                    FPImmLeaf<f64, [{
      return AArch64_AM::isAdvSIMDModImmType10(
                 Imm.bitcastToAPInt().getZExtValue());
    }], SDNodeXForm<fpimm, [{
      APFloat InVal = N->getValueAPF();
      uint32_t enc = AArch64_AM::encodeAdvSIMDModImmType10(N->getValueAPF()
                                                           .bitcastToAPInt()
                                                           .getZExtValue());
      return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i32);
    }]>> {
  let ParserMatchClass = SIMDImmType10Operand;
  let PrintMethod = "printSIMDType10Operand";
}


//---
// System management
//---

// Base encoding for system instruction operands.
let mayLoad = 0, mayStore = 0, hasSideEffects = 1 in
class BaseSystemI<bit L, dag oops, dag iops, string asm, string operands,
                  list<dag> pattern = []>
    : I<oops, iops, asm, operands, "", pattern> {
  let Inst{31-22} = 0b1101010100;
  let Inst{21}    = L;
}

// System instructions which do not have an Rt register.
class SimpleSystemI<bit L, dag iops, string asm, string operands,
                    list<dag> pattern = []>
    : BaseSystemI<L, (outs), iops, asm, operands, pattern> {
  let Inst{4-0} = 0b11111;
}

// System instructions which have an Rt register.
class RtSystemI<bit L, dag oops, dag iops, string asm, string operands>
    : BaseSystemI<L, oops, iops, asm, operands>,
      Sched<[WriteSys]> {
  bits<5> Rt;
  let Inst{4-0} = Rt;
}

// Hint instructions that take both a CRm and a 3-bit immediate.
// NOTE: ideally, this would have mayStore = 0, mayLoad = 0, but we cannot
// model patterns with sufficiently fine granularity
let mayStore = 1, mayLoad = 1, hasSideEffects = 1 in
  class HintI<string mnemonic>
      : SimpleSystemI<0, (ins imm0_127:$imm), mnemonic#"\t$imm", "",
                      [(int_aarch64_hint imm0_127:$imm)]>,
        Sched<[WriteHint]> {
    bits <7> imm;
    let Inst{20-12} = 0b000110010;
    let Inst{11-5} = imm;
  }

// System instructions taking a single literal operand which encodes into
// CRm. op2 differentiates the opcodes.
def BarrierAsmOperand : AsmOperandClass {
  let Name = "Barrier";
  let ParserMethod = "tryParseBarrierOperand";
}
def barrier_op : Operand<i32> {
  let PrintMethod = "printBarrierOption";
  let ParserMatchClass = BarrierAsmOperand;
}
class CRmSystemI<Operand crmtype, bits<3> opc, string asm,
                 list<dag> pattern = []>
    : SimpleSystemI<0, (ins crmtype:$CRm), asm, "\t$CRm", pattern>,
      Sched<[WriteBarrier]> {
  bits<4> CRm;
  let Inst{20-12} = 0b000110011;
  let Inst{11-8} = CRm;
  let Inst{7-5} = opc;
}

class SystemNoOperands<bits<3> op2, string asm, list<dag> pattern = []>
    : SimpleSystemI<0, (ins), asm, "", pattern>,
      Sched<[]> {
  bits<4> CRm;
  let CRm = 0b0011;
  let Inst{31-12} = 0b11010101000000110010;
  let Inst{11-8} = CRm;
  let Inst{7-5} = op2;
  let Inst{4-0} = 0b11111;
}

// MRS/MSR system instructions. These have different operand classes because
// a different subset of registers can be accessed through each instruction.
def MRSSystemRegisterOperand : AsmOperandClass {
  let Name = "MRSSystemRegister";
  let ParserMethod = "tryParseSysReg";
  let DiagnosticType = "MRS";
}
// concatenation of op0, op1, CRn, CRm, op2. 16-bit immediate.
def mrs_sysreg_op : Operand<i32> {
  let ParserMatchClass = MRSSystemRegisterOperand;
  let DecoderMethod = "DecodeMRSSystemRegister";
  let PrintMethod = "printMRSSystemRegister";
}

def MSRSystemRegisterOperand : AsmOperandClass {
  let Name = "MSRSystemRegister";
  let ParserMethod = "tryParseSysReg";
  let DiagnosticType = "MSR";
}
def msr_sysreg_op : Operand<i32> {
  let ParserMatchClass = MSRSystemRegisterOperand;
  let DecoderMethod = "DecodeMSRSystemRegister";
  let PrintMethod = "printMSRSystemRegister";
}

def PSBHintOperand : AsmOperandClass {
  let Name = "PSBHint";
  let ParserMethod = "tryParsePSBHint";
}
def psbhint_op : Operand<i32> {
  let ParserMatchClass = PSBHintOperand;
  let PrintMethod = "printPSBHintOp";
  let MCOperandPredicate = [{
    // Check, if operand is valid, to fix exhaustive aliasing in disassembly.
    // "psb" is an alias to "hint" only for certain values of CRm:Op2 fields.
    if (!MCOp.isImm())
      return false;
    return AArch64PSBHint::lookupPSBByEncoding(MCOp.getImm()) != nullptr;
  }];
}

def BTIHintOperand : AsmOperandClass {
  let Name = "BTIHint";
  let ParserMethod = "tryParseBTIHint";
}
def btihint_op : Operand<i32> {
  let ParserMatchClass = BTIHintOperand;
  let PrintMethod = "printBTIHintOp";
  let MCOperandPredicate = [{
    // "bti" is an alias to "hint" only for certain values of CRm:Op2 fields.
    if (!MCOp.isImm())
      return false;
    return AArch64BTIHint::lookupBTIByEncoding((MCOp.getImm() ^ 32) >> 1) != nullptr;
  }];
}

class MRSI : RtSystemI<1, (outs GPR64:$Rt), (ins mrs_sysreg_op:$systemreg),
                       "mrs", "\t$Rt, $systemreg"> {
  bits<16> systemreg;
  let Inst{20-5} = systemreg;
}

// FIXME: Some of these def NZCV, others don't. Best way to model that?
// Explicitly modeling each of the system register as a register class
// would do it, but feels like overkill at this point.
class MSRI : RtSystemI<0, (outs), (ins msr_sysreg_op:$systemreg, GPR64:$Rt),
                       "msr", "\t$systemreg, $Rt"> {
  bits<16> systemreg;
  let Inst{20-5} = systemreg;
}

def SystemPStateFieldWithImm0_15Operand : AsmOperandClass {
  let Name = "SystemPStateFieldWithImm0_15";
  let ParserMethod = "tryParseSysReg";
}
def pstatefield4_op : Operand<i32> {
  let ParserMatchClass = SystemPStateFieldWithImm0_15Operand;
  let PrintMethod = "printSystemPStateField";
}

// Instructions to modify PSTATE, no input reg
let Defs = [NZCV] in
class PstateWriteSimple<dag iops, string asm, string operands>
  : SimpleSystemI<0, iops, asm, operands> {

  let Inst{20-19} = 0b00;
  let Inst{15-12} = 0b0100;
}

class MSRpstateImm0_15
  : PstateWriteSimple<(ins pstatefield4_op:$pstatefield, imm0_15:$imm), "msr",
                  "\t$pstatefield, $imm">,
    Sched<[WriteSys]> {

  bits<6> pstatefield;
  bits<4> imm;
  let Inst{18-16} = pstatefield{5-3};
  let Inst{11-8} = imm;
  let Inst{7-5} = pstatefield{2-0};

  let DecoderMethod = "DecodeSystemPStateInstruction";
  // MSRpstateI aliases with MSRI. When the MSRpstateI decoder method returns
  // Fail the decoder should attempt to decode the instruction as MSRI.
  let hasCompleteDecoder = 0;
}

def SystemPStateFieldWithImm0_1Operand : AsmOperandClass {
  let Name = "SystemPStateFieldWithImm0_1";
  let ParserMethod = "tryParseSysReg";
}
def pstatefield1_op : Operand<i32> {
  let ParserMatchClass = SystemPStateFieldWithImm0_1Operand;
  let PrintMethod = "printSystemPStateField";
}

class MSRpstateImm0_1
  : PstateWriteSimple<(ins pstatefield1_op:$pstatefield, imm0_1:$imm), "msr",
                 "\t$pstatefield, $imm">,
    Sched<[WriteSys]> {

  bits<6> pstatefield;
  bit imm;
  let Inst{18-16} = pstatefield{5-3};
  let Inst{11-9} = 0b000;
  let Inst{8} = imm;
  let Inst{7-5} = pstatefield{2-0};

  let DecoderMethod = "DecodeSystemPStateInstruction";
  // MSRpstateI aliases with MSRI. When the MSRpstateI decoder method returns
  // Fail the decoder should attempt to decode the instruction as MSRI.
  let hasCompleteDecoder = 0;
}

// SYS and SYSL generic system instructions.
def SysCRAsmOperand : AsmOperandClass {
  let Name = "SysCR";
  let ParserMethod = "tryParseSysCROperand";
}

def sys_cr_op : Operand<i32> {
  let PrintMethod = "printSysCROperand";
  let ParserMatchClass = SysCRAsmOperand;
}

class SystemXtI<bit L, string asm>
  : RtSystemI<L, (outs),
       (ins imm0_7:$op1, sys_cr_op:$Cn, sys_cr_op:$Cm, imm0_7:$op2, GPR64:$Rt),
       asm, "\t$op1, $Cn, $Cm, $op2, $Rt"> {
  bits<3> op1;
  bits<4> Cn;
  bits<4> Cm;
  bits<3> op2;
  let Inst{20-19} = 0b01;
  let Inst{18-16} = op1;
  let Inst{15-12} = Cn;
  let Inst{11-8}  = Cm;
  let Inst{7-5}   = op2;
}

class SystemLXtI<bit L, string asm>
  : RtSystemI<L, (outs),
       (ins GPR64:$Rt, imm0_7:$op1, sys_cr_op:$Cn, sys_cr_op:$Cm, imm0_7:$op2),
       asm, "\t$Rt, $op1, $Cn, $Cm, $op2"> {
  bits<3> op1;
  bits<4> Cn;
  bits<4> Cm;
  bits<3> op2;
  let Inst{20-19} = 0b01;
  let Inst{18-16} = op1;
  let Inst{15-12} = Cn;
  let Inst{11-8}  = Cm;
  let Inst{7-5}   = op2;
}


// Branch (register) instructions:
//
//  case opc of
//    0001 blr
//    0000 br
//    0101 dret
//    0100 eret
//    0010 ret
//    otherwise UNDEFINED
class BaseBranchReg<bits<4> opc, dag oops, dag iops, string asm,
                    string operands, list<dag> pattern>
    : I<oops, iops, asm, operands, "", pattern>, Sched<[WriteBrReg]> {
  let Inst{31-25} = 0b1101011;
  let Inst{24-21} = opc;
  let Inst{20-16} = 0b11111;
  let Inst{15-10} = 0b000000;
  let Inst{4-0}   = 0b00000;
}

class BranchReg<bits<4> opc, string asm, list<dag> pattern>
    : BaseBranchReg<opc, (outs), (ins GPR64:$Rn), asm, "\t$Rn", pattern> {
  bits<5> Rn;
  let Inst{9-5} = Rn;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 1, isReturn = 1 in
class SpecialReturn<bits<4> opc, string asm>
    : BaseBranchReg<opc, (outs), (ins), asm, "", []> {
  let Inst{9-5} = 0b11111;
}

let mayLoad = 1 in
class RCPCLoad<bits<2> sz, string asm, RegisterClass RC>
  : I<(outs RC:$Rt), (ins GPR64sp0:$Rn), asm, "\t$Rt, [$Rn]", "", []>,
  Sched<[]> {
  bits<5> Rn;
  bits<5> Rt;
  let Inst{31-30} = sz;
  let Inst{29-10} = 0b11100010111111110000;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rt;
}

class AuthBase<bits<1> M, dag oops, dag iops, string asm, string operands,
               list<dag> pattern>
  : I<oops, iops, asm, operands, "", pattern>, Sched<[]> {
  let Inst{31-25} = 0b1101011;
  let Inst{20-11} = 0b1111100001;
  let Inst{10} = M;
  let Inst{4-0} = 0b11111;
}

class AuthBranchTwoOperands<bits<1> op, bits<1> M, string asm>
  : AuthBase<M, (outs), (ins GPR64:$Rn, GPR64sp:$Rm), asm, "\t$Rn, $Rm", []> {
  bits<5> Rn;
  bits<5> Rm;
  let Inst{24-22} = 0b100;
  let Inst{21} = op;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rm;
}

class AuthOneOperand<bits<3> opc, bits<1> M, string asm>
  : AuthBase<M, (outs), (ins GPR64:$Rn), asm, "\t$Rn", []> {
  bits<5> Rn;
  let Inst{24} = 0;
  let Inst{23-21} = opc;
  let Inst{9-5} = Rn;
}

class AuthReturn<bits<3> op, bits<1> M, string asm>
  : AuthBase<M, (outs), (ins), asm, "", []> {
  let Inst{24} = 0;
  let Inst{23-21} = op;
  let Inst{9-0} = 0b1111111111;
}

let mayLoad = 1 in
class BaseAuthLoad<bit M, bit W, dag oops, dag iops, string asm,
                   string operands, string cstr, Operand opr>
  : I<oops, iops, asm, operands, cstr, []>, Sched<[]> {
  bits<10> offset;
  bits<5> Rn;
  bits<5> Rt;
  let Inst{31-24} = 0b11111000;
  let Inst{23} = M;
  let Inst{22} = offset{9};
  let Inst{21} = 1;
  let Inst{20-12} = offset{8-0};
  let Inst{11} = W;
  let Inst{10} = 1;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rt;
}

multiclass AuthLoad<bit M, string asm, Operand opr> {
  def indexed   : BaseAuthLoad<M, 0, (outs GPR64:$Rt),
                               (ins GPR64sp:$Rn, opr:$offset),
                               asm, "\t$Rt, [$Rn, $offset]", "", opr>;
  def writeback : BaseAuthLoad<M, 1, (outs GPR64sp:$wback, GPR64:$Rt),
                               (ins GPR64sp:$Rn, opr:$offset),
                               asm, "\t$Rt, [$Rn, $offset]!",
                               "$Rn = $wback,@earlyclobber $wback", opr>;

  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "indexed") GPR64:$Rt, GPR64sp:$Rn, 0)>;
}

//---
// Conditional branch instruction.
//---

// Condition code.
// 4-bit immediate. Pretty-printed as <cc>
def ccode : Operand<i32> {
  let PrintMethod = "printCondCode";
  let ParserMatchClass = CondCode;
}
def inv_ccode : Operand<i32> {
  // AL and NV are invalid in the aliases which use inv_ccode
  let PrintMethod = "printInverseCondCode";
  let ParserMatchClass = CondCode;
  let MCOperandPredicate = [{
    return MCOp.isImm() &&
           MCOp.getImm() != AArch64CC::AL &&
           MCOp.getImm() != AArch64CC::NV;
  }];
}

// Conditional branch target. 19-bit immediate. The low two bits of the target
// offset are implied zero and so are not part of the immediate.
def am_brcond : Operand<OtherVT> {
  let EncoderMethod = "getCondBranchTargetOpValue";
  let DecoderMethod = "DecodePCRelLabel19";
  let PrintMethod = "printAlignedLabel";
  let ParserMatchClass = PCRelLabel19Operand;
  let OperandType = "OPERAND_PCREL";
}

class BranchCond : I<(outs), (ins ccode:$cond, am_brcond:$target),
                     "b", ".$cond\t$target", "",
                     [(AArch64brcond bb:$target, imm:$cond, NZCV)]>,
                   Sched<[WriteBr]> {
  let isBranch = 1;
  let isTerminator = 1;
  let Uses = [NZCV];

  bits<4> cond;
  bits<19> target;
  let Inst{31-24} = 0b01010100;
  let Inst{23-5} = target;
  let Inst{4} = 0;
  let Inst{3-0} = cond;
}

//---
// Compare-and-branch instructions.
//---
class BaseCmpBranch<RegisterClass regtype, bit op, string asm, SDNode node>
    : I<(outs), (ins regtype:$Rt, am_brcond:$target),
         asm, "\t$Rt, $target", "",
         [(node regtype:$Rt, bb:$target)]>,
      Sched<[WriteBr]> {
  let isBranch = 1;
  let isTerminator = 1;

  bits<5> Rt;
  bits<19> target;
  let Inst{30-25} = 0b011010;
  let Inst{24}    = op;
  let Inst{23-5}  = target;
  let Inst{4-0}   = Rt;
}

multiclass CmpBranch<bit op, string asm, SDNode node> {
  def W : BaseCmpBranch<GPR32, op, asm, node> {
    let Inst{31} = 0;
  }
  def X : BaseCmpBranch<GPR64, op, asm, node> {
    let Inst{31} = 1;
  }
}

//---
// Test-bit-and-branch instructions.
//---
// Test-and-branch target. 14-bit sign-extended immediate. The low two bits of
// the target offset are implied zero and so are not part of the immediate.
def am_tbrcond : Operand<OtherVT> {
  let EncoderMethod = "getTestBranchTargetOpValue";
  let PrintMethod = "printAlignedLabel";
  let ParserMatchClass = BranchTarget14Operand;
  let OperandType = "OPERAND_PCREL";
}

// AsmOperand classes to emit (or not) special diagnostics
def TBZImm0_31Operand : AsmOperandClass {
  let Name = "TBZImm0_31";
  let PredicateMethod = "isImmInRange<0,31>";
  let RenderMethod = "addImmOperands";
}
def TBZImm32_63Operand : AsmOperandClass {
  let Name = "Imm32_63";
  let PredicateMethod = "isImmInRange<32,63>";
  let DiagnosticType = "InvalidImm0_63";
  let RenderMethod = "addImmOperands";
}

class tbz_imm0_31<AsmOperandClass matcher> : Operand<i64>, ImmLeaf<i64, [{
  return (((uint32_t)Imm) < 32);
}]> {
  let ParserMatchClass = matcher;
}

def tbz_imm0_31_diag : tbz_imm0_31<Imm0_31Operand>;
def tbz_imm0_31_nodiag : tbz_imm0_31<TBZImm0_31Operand>;

def tbz_imm32_63 : Operand<i64>, ImmLeaf<i64, [{
  return (((uint32_t)Imm) > 31) && (((uint32_t)Imm) < 64);
}]> {
  let ParserMatchClass = TBZImm32_63Operand;
}

class BaseTestBranch<RegisterClass regtype, Operand immtype,
                     bit op, string asm, SDNode node>
    : I<(outs), (ins regtype:$Rt, immtype:$bit_off, am_tbrcond:$target),
       asm, "\t$Rt, $bit_off, $target", "",
       [(node regtype:$Rt, immtype:$bit_off, bb:$target)]>,
      Sched<[WriteBr]> {
  let isBranch = 1;
  let isTerminator = 1;

  bits<5> Rt;
  bits<6> bit_off;
  bits<14> target;

  let Inst{30-25} = 0b011011;
  let Inst{24}    = op;
  let Inst{23-19} = bit_off{4-0};
  let Inst{18-5}  = target;
  let Inst{4-0}   = Rt;

  let DecoderMethod = "DecodeTestAndBranch";
}

multiclass TestBranch<bit op, string asm, SDNode node> {
  def W : BaseTestBranch<GPR32, tbz_imm0_31_diag, op, asm, node> {
    let Inst{31} = 0;
  }

  def X : BaseTestBranch<GPR64, tbz_imm32_63, op, asm, node> {
    let Inst{31} = 1;
  }

  // Alias X-reg with 0-31 imm to W-Reg.
  def : InstAlias<asm # "\t$Rd, $imm, $target",
                  (!cast<Instruction>(NAME#"W") GPR32as64:$Rd,
                  tbz_imm0_31_nodiag:$imm, am_tbrcond:$target), 0>;
  def : Pat<(node GPR64:$Rn, tbz_imm0_31_diag:$imm, bb:$target),
            (!cast<Instruction>(NAME#"W") (EXTRACT_SUBREG GPR64:$Rn, sub_32),
            tbz_imm0_31_diag:$imm, bb:$target)>;
}

//---
// Unconditional branch (immediate) instructions.
//---
def am_b_target : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValue";
  let PrintMethod = "printAlignedLabel";
  let ParserMatchClass = BranchTarget26Operand;
  let OperandType = "OPERAND_PCREL";
}
def am_bl_target : Operand<i64> {
  let EncoderMethod = "getBranchTargetOpValue";
  let PrintMethod = "printAlignedLabel";
  let ParserMatchClass = BranchTarget26Operand;
  let OperandType = "OPERAND_PCREL";
}

class BImm<bit op, dag iops, string asm, list<dag> pattern>
    : I<(outs), iops, asm, "\t$addr", "", pattern>, Sched<[WriteBr]> {
  bits<26> addr;
  let Inst{31}    = op;
  let Inst{30-26} = 0b00101;
  let Inst{25-0}  = addr;

  let DecoderMethod = "DecodeUnconditionalBranch";
}

class BranchImm<bit op, string asm, list<dag> pattern>
    : BImm<op, (ins am_b_target:$addr), asm, pattern>;
class CallImm<bit op, string asm, list<dag> pattern>
    : BImm<op, (ins am_bl_target:$addr), asm, pattern>;

//---
// Basic one-operand data processing instructions.
//---

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseOneOperandData<bits<3> opc, RegisterClass regtype, string asm,
                         SDPatternOperator node>
  : I<(outs regtype:$Rd), (ins regtype:$Rn), asm, "\t$Rd, $Rn", "",
      [(set regtype:$Rd, (node regtype:$Rn))]>,
    Sched<[WriteI, ReadI]> {
  bits<5> Rd;
  bits<5> Rn;

  let Inst{30-13} = 0b101101011000000000;
  let Inst{12-10} = opc;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
multiclass OneOperandData<bits<3> opc, string asm,
                          SDPatternOperator node = null_frag> {
  def Wr : BaseOneOperandData<opc, GPR32, asm, node> {
    let Inst{31} = 0;
  }

  def Xr : BaseOneOperandData<opc, GPR64, asm, node> {
    let Inst{31} = 1;
  }
}

class OneWRegData<bits<3> opc, string asm, SDPatternOperator node>
    : BaseOneOperandData<opc, GPR32, asm, node> {
  let Inst{31} = 0;
}

class OneXRegData<bits<3> opc, string asm, SDPatternOperator node>
    : BaseOneOperandData<opc, GPR64, asm, node> {
  let Inst{31} = 1;
}

class SignAuthOneData<bits<3> opcode_prefix, bits<2> opcode, string asm>
  : I<(outs GPR64:$Rd), (ins GPR64sp:$Rn), asm, "\t$Rd, $Rn", "",
      []>,
    Sched<[WriteI, ReadI]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31-15} = 0b11011010110000010;
  let Inst{14-12} = opcode_prefix;
  let Inst{11-10} = opcode;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rd;
}

class SignAuthZero<bits<3> opcode_prefix, bits<2> opcode, string asm>
  : I<(outs GPR64:$Rd), (ins), asm, "\t$Rd", "", []>, Sched<[]> {
  bits<5> Rd;
  let Inst{31-15} = 0b11011010110000010;
  let Inst{14-12} = opcode_prefix;
  let Inst{11-10} = opcode;
  let Inst{9-5} = 0b11111;
  let Inst{4-0} = Rd;
}

class SignAuthTwoOperand<bits<4> opc, string asm,
                         SDPatternOperator OpNode>
  : I<(outs GPR64:$Rd), (ins GPR64:$Rn, GPR64sp:$Rm),
      asm, "\t$Rd, $Rn, $Rm", "",
      [(set GPR64:$Rd, (OpNode GPR64:$Rn, GPR64sp:$Rm))]>,
    Sched<[WriteI, ReadI, ReadI]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31-21} = 0b10011010110;
  let Inst{20-16} = Rm;
  let Inst{15-14} = 0b00;
  let Inst{13-10} = opc;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

// Base class for the Armv8.4-A 8 and 16-bit flag manipulation instructions
class BaseFlagManipulation<bit sf, bit sz, dag iops, string asm, string ops>
    : I<(outs), iops, asm, ops, "", []>,
      Sched<[WriteI, ReadI, ReadI]> {
  let Uses = [NZCV];
  bits<5> Rn;
  let Inst{31}    = sf;
  let Inst{30-15} = 0b0111010000000000;
  let Inst{14}    = sz;
  let Inst{13-10} = 0b0010;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = 0b01101;
}

class FlagRotate<dag iops, string asm, string ops>
    : BaseFlagManipulation<0b1, 0b0, iops, asm, ops> {
  bits<6> imm;
  bits<4> mask;
  let Inst{20-15} = imm;
  let Inst{13-10} = 0b0001;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = mask;
}

//---
// Basic two-operand data processing instructions.
//---
class BaseBaseAddSubCarry<bit isSub, RegisterClass regtype, string asm,
                          list<dag> pattern>
    : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm),
        asm, "\t$Rd, $Rn, $Rm", "", pattern>,
      Sched<[WriteI, ReadI, ReadI]> {
  let Uses = [NZCV];
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{30}    = isSub;
  let Inst{28-21} = 0b11010000;
  let Inst{20-16} = Rm;
  let Inst{15-10} = 0;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

class BaseAddSubCarry<bit isSub, RegisterClass regtype, string asm,
                      SDNode OpNode>
    : BaseBaseAddSubCarry<isSub, regtype, asm,
        [(set regtype:$Rd, (OpNode regtype:$Rn, regtype:$Rm, NZCV))]>;

class BaseAddSubCarrySetFlags<bit isSub, RegisterClass regtype, string asm,
                              SDNode OpNode>
    : BaseBaseAddSubCarry<isSub, regtype, asm,
        [(set regtype:$Rd, (OpNode regtype:$Rn, regtype:$Rm, NZCV)),
         (implicit NZCV)]> {
  let Defs = [NZCV];
}

multiclass AddSubCarry<bit isSub, string asm, string asm_setflags,
                       SDNode OpNode, SDNode OpNode_setflags> {
  def Wr : BaseAddSubCarry<isSub, GPR32, asm, OpNode> {
    let Inst{31} = 0;
    let Inst{29} = 0;
  }
  def Xr : BaseAddSubCarry<isSub, GPR64, asm, OpNode> {
    let Inst{31} = 1;
    let Inst{29} = 0;
  }

  // Sets flags.
  def SWr : BaseAddSubCarrySetFlags<isSub, GPR32, asm_setflags,
                                    OpNode_setflags> {
    let Inst{31} = 0;
    let Inst{29} = 1;
  }
  def SXr : BaseAddSubCarrySetFlags<isSub, GPR64, asm_setflags,
                                    OpNode_setflags> {
    let Inst{31} = 1;
    let Inst{29} = 1;
  }
}

class BaseTwoOperand<bits<4> opc, RegisterClass regtype, string asm,
                     SDPatternOperator OpNode,
                     RegisterClass in1regtype = regtype,
                     RegisterClass in2regtype = regtype>
  : I<(outs regtype:$Rd), (ins in1regtype:$Rn, in2regtype:$Rm),
      asm, "\t$Rd, $Rn, $Rm", "",
      [(set regtype:$Rd, (OpNode in1regtype:$Rn, in2regtype:$Rm))]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{30-21} = 0b0011010110;
  let Inst{20-16} = Rm;
  let Inst{15-14} = 0b00;
  let Inst{13-10} = opc;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

class BaseDiv<bit isSigned, RegisterClass regtype, string asm,
              SDPatternOperator OpNode>
    : BaseTwoOperand<{0,0,1,?}, regtype, asm, OpNode> {
  let Inst{10}    = isSigned;
}

multiclass Div<bit isSigned, string asm, SDPatternOperator OpNode> {
  def Wr : BaseDiv<isSigned, GPR32, asm, OpNode>,
           Sched<[WriteID32, ReadID, ReadID]> {
    let Inst{31} = 0;
  }
  def Xr : BaseDiv<isSigned, GPR64, asm, OpNode>,
           Sched<[WriteID64, ReadID, ReadID]> {
    let Inst{31} = 1;
  }
}

class BaseShift<bits<2> shift_type, RegisterClass regtype, string asm,
                SDPatternOperator OpNode = null_frag>
  : BaseTwoOperand<{1,0,?,?}, regtype, asm, OpNode>,
    Sched<[WriteIS, ReadI]> {
  let Inst{11-10} = shift_type;
}

multiclass Shift<bits<2> shift_type, string asm, SDNode OpNode> {
  def Wr : BaseShift<shift_type, GPR32, asm> {
    let Inst{31} = 0;
  }

  def Xr : BaseShift<shift_type, GPR64, asm, OpNode> {
    let Inst{31} = 1;
  }

  def : Pat<(i32 (OpNode GPR32:$Rn, i64:$Rm)),
            (!cast<Instruction>(NAME # "Wr") GPR32:$Rn,
                                             (EXTRACT_SUBREG i64:$Rm, sub_32))>;

  def : Pat<(i32 (OpNode GPR32:$Rn, (i64 (zext GPR32:$Rm)))),
            (!cast<Instruction>(NAME # "Wr") GPR32:$Rn, GPR32:$Rm)>;

  def : Pat<(i32 (OpNode GPR32:$Rn, (i64 (anyext GPR32:$Rm)))),
            (!cast<Instruction>(NAME # "Wr") GPR32:$Rn, GPR32:$Rm)>;

  def : Pat<(i32 (OpNode GPR32:$Rn, (i64 (sext GPR32:$Rm)))),
            (!cast<Instruction>(NAME # "Wr") GPR32:$Rn, GPR32:$Rm)>;
}

class ShiftAlias<string asm, Instruction inst, RegisterClass regtype>
    : InstAlias<asm#"\t$dst, $src1, $src2",
                (inst regtype:$dst, regtype:$src1, regtype:$src2), 0>;

class BaseMulAccum<bit isSub, bits<3> opc, RegisterClass multype,
                       RegisterClass addtype, string asm,
                       list<dag> pattern>
  : I<(outs addtype:$Rd), (ins multype:$Rn, multype:$Rm, addtype:$Ra),
      asm, "\t$Rd, $Rn, $Rm, $Ra", "", pattern> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<5> Ra;
  let Inst{30-24} = 0b0011011;
  let Inst{23-21} = opc;
  let Inst{20-16} = Rm;
  let Inst{15}    = isSub;
  let Inst{14-10} = Ra;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass MulAccum<bit isSub, string asm, SDNode AccNode> {
  // MADD/MSUB generation is decided by MachineCombiner.cpp
  def Wrrr : BaseMulAccum<isSub, 0b000, GPR32, GPR32, asm,
      [/*(set GPR32:$Rd, (AccNode GPR32:$Ra, (mul GPR32:$Rn, GPR32:$Rm)))*/]>,
      Sched<[WriteIM32, ReadIM, ReadIM, ReadIMA]> {
    let Inst{31} = 0;
  }

  def Xrrr : BaseMulAccum<isSub, 0b000, GPR64, GPR64, asm,
      [/*(set GPR64:$Rd, (AccNode GPR64:$Ra, (mul GPR64:$Rn, GPR64:$Rm)))*/]>,
      Sched<[WriteIM64, ReadIM, ReadIM, ReadIMA]> {
    let Inst{31} = 1;
  }
}

class WideMulAccum<bit isSub, bits<3> opc, string asm,
                   SDNode AccNode, SDNode ExtNode>
  : BaseMulAccum<isSub, opc, GPR32, GPR64, asm,
    [(set GPR64:$Rd, (AccNode GPR64:$Ra,
                            (mul (ExtNode GPR32:$Rn), (ExtNode GPR32:$Rm))))]>,
    Sched<[WriteIM32, ReadIM, ReadIM, ReadIMA]> {
  let Inst{31} = 1;
}

class MulHi<bits<3> opc, string asm, SDNode OpNode>
  : I<(outs GPR64:$Rd), (ins GPR64:$Rn, GPR64:$Rm),
      asm, "\t$Rd, $Rn, $Rm", "",
      [(set GPR64:$Rd, (OpNode GPR64:$Rn, GPR64:$Rm))]>,
    Sched<[WriteIM64, ReadIM, ReadIM]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31-24} = 0b10011011;
  let Inst{23-21} = opc;
  let Inst{20-16} = Rm;
  let Inst{15}    = 0;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;

  // The Ra field of SMULH and UMULH is unused: it should be assembled as 31
  // (i.e. all bits 1) but is ignored by the processor.
  let PostEncoderMethod = "fixMulHigh";
}

class MulAccumWAlias<string asm, Instruction inst>
    : InstAlias<asm#"\t$dst, $src1, $src2",
                (inst GPR32:$dst, GPR32:$src1, GPR32:$src2, WZR)>;
class MulAccumXAlias<string asm, Instruction inst>
    : InstAlias<asm#"\t$dst, $src1, $src2",
                (inst GPR64:$dst, GPR64:$src1, GPR64:$src2, XZR)>;
class WideMulAccumAlias<string asm, Instruction inst>
    : InstAlias<asm#"\t$dst, $src1, $src2",
                (inst GPR64:$dst, GPR32:$src1, GPR32:$src2, XZR)>;

class BaseCRC32<bit sf, bits<2> sz, bit C, RegisterClass StreamReg,
              SDPatternOperator OpNode, string asm>
  : I<(outs GPR32:$Rd), (ins GPR32:$Rn, StreamReg:$Rm),
      asm, "\t$Rd, $Rn, $Rm", "",
      [(set GPR32:$Rd, (OpNode GPR32:$Rn, StreamReg:$Rm))]>,
    Sched<[WriteISReg, ReadI, ReadISReg]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;

  let Inst{31} = sf;
  let Inst{30-21} = 0b0011010110;
  let Inst{20-16} = Rm;
  let Inst{15-13} = 0b010;
  let Inst{12} = C;
  let Inst{11-10} = sz;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rd;
  let Predicates = [HasCRC];
}

//---
// Address generation.
//---

class ADRI<bit page, string asm, Operand adr, list<dag> pattern>
    : I<(outs GPR64:$Xd), (ins adr:$label), asm, "\t$Xd, $label", "",
        pattern>,
      Sched<[WriteI]> {
  bits<5>  Xd;
  bits<21> label;
  let Inst{31}    = page;
  let Inst{30-29} = label{1-0};
  let Inst{28-24} = 0b10000;
  let Inst{23-5}  = label{20-2};
  let Inst{4-0}   = Xd;

  let DecoderMethod = "DecodeAdrInstruction";
}

//---
// Move immediate.
//---

def movimm32_imm : Operand<i32> {
  let ParserMatchClass = AsmImmRange<0, 65535>;
  let EncoderMethod = "getMoveWideImmOpValue";
  let PrintMethod = "printImm";
}
def movimm32_shift : Operand<i32> {
  let PrintMethod = "printShifter";
  let ParserMatchClass = MovImm32ShifterOperand;
}
def movimm64_shift : Operand<i32> {
  let PrintMethod = "printShifter";
  let ParserMatchClass = MovImm64ShifterOperand;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseMoveImmediate<bits<2> opc, RegisterClass regtype, Operand shifter,
                        string asm>
  : I<(outs regtype:$Rd), (ins movimm32_imm:$imm, shifter:$shift),
       asm, "\t$Rd, $imm$shift", "", []>,
    Sched<[WriteImm]> {
  bits<5> Rd;
  bits<16> imm;
  bits<6> shift;
  let Inst{30-29} = opc;
  let Inst{28-23} = 0b100101;
  let Inst{22-21} = shift{5-4};
  let Inst{20-5}  = imm;
  let Inst{4-0}   = Rd;

  let DecoderMethod = "DecodeMoveImmInstruction";
}

multiclass MoveImmediate<bits<2> opc, string asm> {
  def Wi : BaseMoveImmediate<opc, GPR32, movimm32_shift, asm> {
    let Inst{31} = 0;
  }

  def Xi : BaseMoveImmediate<opc, GPR64, movimm64_shift, asm> {
    let Inst{31} = 1;
  }
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseInsertImmediate<bits<2> opc, RegisterClass regtype, Operand shifter,
                          string asm>
  : I<(outs regtype:$Rd),
      (ins regtype:$src, movimm32_imm:$imm, shifter:$shift),
       asm, "\t$Rd, $imm$shift", "$src = $Rd", []>,
    Sched<[WriteI, ReadI]> {
  bits<5> Rd;
  bits<16> imm;
  bits<6> shift;
  let Inst{30-29} = opc;
  let Inst{28-23} = 0b100101;
  let Inst{22-21} = shift{5-4};
  let Inst{20-5}  = imm;
  let Inst{4-0}   = Rd;

  let DecoderMethod = "DecodeMoveImmInstruction";
}

multiclass InsertImmediate<bits<2> opc, string asm> {
  def Wi : BaseInsertImmediate<opc, GPR32, movimm32_shift, asm> {
    let Inst{31} = 0;
  }

  def Xi : BaseInsertImmediate<opc, GPR64, movimm64_shift, asm> {
    let Inst{31} = 1;
  }
}

//---
// Add/Subtract
//---

class BaseAddSubImm<bit isSub, bit setFlags, RegisterClass dstRegtype,
                    string asm_inst, string asm_ops,
                    dag inputs, dag pattern>
    : I<(outs dstRegtype:$Rd), inputs, asm_inst, asm_ops, "", [pattern]>,
      Sched<[WriteI, ReadI]> {
  bits<5>  Rd;
  bits<5>  Rn;
  let Inst{30}    = isSub;
  let Inst{29}    = setFlags;
  let Inst{28-24} = 0b10001;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

class AddSubImmShift<bit isSub, bit setFlags, RegisterClass dstRegtype,
                     RegisterClass srcRegtype, addsub_shifted_imm immtype,
                     string asm_inst, SDPatternOperator OpNode>
    : BaseAddSubImm<isSub, setFlags, dstRegtype, asm_inst, "\t$Rd, $Rn, $imm",
                    (ins srcRegtype:$Rn, immtype:$imm),
                    (set dstRegtype:$Rd, (OpNode srcRegtype:$Rn, immtype:$imm))> {
  bits<14> imm;
  let Inst{23-22} = imm{13-12}; // '00' => lsl #0, '01' => lsl #12
  let Inst{21-10} = imm{11-0};
  let DecoderMethod = "DecodeAddSubImmShift";
}

class BaseAddSubRegPseudo<RegisterClass regtype,
                          SDPatternOperator OpNode>
    : Pseudo<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm),
             [(set regtype:$Rd, (OpNode regtype:$Rn, regtype:$Rm))]>,
      Sched<[WriteI, ReadI, ReadI]>;

class BaseAddSubSReg<bit isSub, bit setFlags, RegisterClass regtype,
                     arith_shifted_reg shifted_regtype, string asm,
                     SDPatternOperator OpNode>
    : I<(outs regtype:$Rd), (ins regtype:$Rn, shifted_regtype:$Rm),
        asm, "\t$Rd, $Rn, $Rm", "",
        [(set regtype:$Rd, (OpNode regtype:$Rn, shifted_regtype:$Rm))]>,
      Sched<[WriteISReg, ReadI, ReadISReg]> {
  // The operands are in order to match the 'addr' MI operands, so we
  // don't need an encoder method and by-name matching. Just use the default
  // in-order handling. Since we're using by-order, make sure the names
  // do not match.
  bits<5> dst;
  bits<5> src1;
  bits<5> src2;
  bits<8> shift;
  let Inst{30}    = isSub;
  let Inst{29}    = setFlags;
  let Inst{28-24} = 0b01011;
  let Inst{23-22} = shift{7-6};
  let Inst{21}    = 0;
  let Inst{20-16} = src2;
  let Inst{15-10} = shift{5-0};
  let Inst{9-5}   = src1;
  let Inst{4-0}   = dst;

  let DecoderMethod = "DecodeThreeAddrSRegInstruction";
}

class BaseAddSubEReg<bit isSub, bit setFlags, RegisterClass dstRegtype,
                     RegisterClass src1Regtype, Operand src2Regtype,
                     string asm, SDPatternOperator OpNode>
    : I<(outs dstRegtype:$R1),
        (ins src1Regtype:$R2, src2Regtype:$R3),
        asm, "\t$R1, $R2, $R3", "",
        [(set dstRegtype:$R1, (OpNode src1Regtype:$R2, src2Regtype:$R3))]>,
      Sched<[WriteIEReg, ReadI, ReadIEReg]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<6> ext;
  let Inst{30}    = isSub;
  let Inst{29}    = setFlags;
  let Inst{28-24} = 0b01011;
  let Inst{23-21} = 0b001;
  let Inst{20-16} = Rm;
  let Inst{15-13} = ext{5-3};
  let Inst{12-10} = ext{2-0};
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;

  let DecoderMethod = "DecodeAddSubERegInstruction";
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseAddSubEReg64<bit isSub, bit setFlags, RegisterClass dstRegtype,
                       RegisterClass src1Regtype, RegisterClass src2Regtype,
                       Operand ext_op, string asm>
    : I<(outs dstRegtype:$Rd),
        (ins src1Regtype:$Rn, src2Regtype:$Rm, ext_op:$ext),
        asm, "\t$Rd, $Rn, $Rm$ext", "", []>,
      Sched<[WriteIEReg, ReadI, ReadIEReg]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<6> ext;
  let Inst{30}    = isSub;
  let Inst{29}    = setFlags;
  let Inst{28-24} = 0b01011;
  let Inst{23-21} = 0b001;
  let Inst{20-16} = Rm;
  let Inst{15}    = ext{5};
  let Inst{12-10} = ext{2-0};
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;

  let DecoderMethod = "DecodeAddSubERegInstruction";
}

// Aliases for register+register add/subtract.
class AddSubRegAlias<string asm, Instruction inst, RegisterClass dstRegtype,
                     RegisterClass src1Regtype, RegisterClass src2Regtype,
                     int shiftExt>
    : InstAlias<asm#"\t$dst, $src1, $src2",
                (inst dstRegtype:$dst, src1Regtype:$src1, src2Regtype:$src2,
                      shiftExt)>;

multiclass AddSub<bit isSub, string mnemonic, string alias,
                  SDPatternOperator OpNode = null_frag> {
  let hasSideEffects = 0, isReMaterializable = 1, isAsCheapAsAMove = 1 in {
  // Add/Subtract immediate
  // Increase the weight of the immediate variant to try to match it before
  // the extended register variant.
  // We used to match the register variant before the immediate when the
  // register argument could be implicitly zero-extended.
  let AddedComplexity = 6 in
  def Wri  : AddSubImmShift<isSub, 0, GPR32sp, GPR32sp, addsub_shifted_imm32,
                           mnemonic, OpNode> {
    let Inst{31} = 0;
  }
  let AddedComplexity = 6 in
  def Xri  : AddSubImmShift<isSub, 0, GPR64sp, GPR64sp, addsub_shifted_imm64,
                           mnemonic, OpNode> {
    let Inst{31} = 1;
  }

  // Add/Subtract register - Only used for CodeGen
  def Wrr : BaseAddSubRegPseudo<GPR32, OpNode>;
  def Xrr : BaseAddSubRegPseudo<GPR64, OpNode>;

  // Add/Subtract shifted register
  def Wrs : BaseAddSubSReg<isSub, 0, GPR32, arith_shifted_reg32, mnemonic,
                           OpNode> {
    let Inst{31} = 0;
  }
  def Xrs : BaseAddSubSReg<isSub, 0, GPR64, arith_shifted_reg64, mnemonic,
                           OpNode> {
    let Inst{31} = 1;
  }
  }

  // Add/Subtract extended register
  let AddedComplexity = 1, hasSideEffects = 0 in {
  def Wrx : BaseAddSubEReg<isSub, 0, GPR32sp, GPR32sp,
                           arith_extended_reg32<i32>, mnemonic, OpNode> {
    let Inst{31} = 0;
  }
  def Xrx : BaseAddSubEReg<isSub, 0, GPR64sp, GPR64sp,
                           arith_extended_reg32to64<i64>, mnemonic, OpNode> {
    let Inst{31} = 1;
  }
  }

  def Xrx64 : BaseAddSubEReg64<isSub, 0, GPR64sp, GPR64sp, GPR64,
                               arith_extendlsl64, mnemonic> {
    // UXTX and SXTX only.
    let Inst{14-13} = 0b11;
    let Inst{31} = 1;
  }

  // add Rd, Rb, -imm -> sub Rd, Rn, imm
  def : InstSubst<alias#"\t$Rd, $Rn, $imm",
                  (!cast<Instruction>(NAME # "Wri") GPR32sp:$Rd, GPR32sp:$Rn,
                      addsub_shifted_imm32_neg:$imm), 0>;
  def : InstSubst<alias#"\t$Rd, $Rn, $imm",
                  (!cast<Instruction>(NAME # "Xri") GPR64sp:$Rd, GPR64sp:$Rn,
                       addsub_shifted_imm64_neg:$imm), 0>;

  // Register/register aliases with no shift when SP is not used.
  def : AddSubRegAlias<mnemonic, !cast<Instruction>(NAME#"Wrs"),
                       GPR32, GPR32, GPR32, 0>;
  def : AddSubRegAlias<mnemonic, !cast<Instruction>(NAME#"Xrs"),
                       GPR64, GPR64, GPR64, 0>;

  // Register/register aliases with no shift when either the destination or
  // first source register is SP.
  def : AddSubRegAlias<mnemonic, !cast<Instruction>(NAME#"Wrx"),
                       GPR32sponly, GPR32sp, GPR32, 16>; // UXTW #0
  def : AddSubRegAlias<mnemonic, !cast<Instruction>(NAME#"Wrx"),
                       GPR32sp, GPR32sponly, GPR32, 16>; // UXTW #0
  def : AddSubRegAlias<mnemonic,
                       !cast<Instruction>(NAME#"Xrx64"),
                       GPR64sponly, GPR64sp, GPR64, 24>; // UXTX #0
  def : AddSubRegAlias<mnemonic,
                       !cast<Instruction>(NAME#"Xrx64"),
                       GPR64sp, GPR64sponly, GPR64, 24>; // UXTX #0
}

multiclass AddSubS<bit isSub, string mnemonic, SDNode OpNode, string cmp,
                   string alias, string cmpAlias> {
  let isCompare = 1, Defs = [NZCV] in {
  // Add/Subtract immediate
  def Wri  : AddSubImmShift<isSub, 1, GPR32, GPR32sp, addsub_shifted_imm32,
                           mnemonic, OpNode> {
    let Inst{31} = 0;
  }
  def Xri  : AddSubImmShift<isSub, 1, GPR64, GPR64sp, addsub_shifted_imm64,
                           mnemonic, OpNode> {
    let Inst{31} = 1;
  }

  // Add/Subtract register
  def Wrr : BaseAddSubRegPseudo<GPR32, OpNode>;
  def Xrr : BaseAddSubRegPseudo<GPR64, OpNode>;

  // Add/Subtract shifted register
  def Wrs : BaseAddSubSReg<isSub, 1, GPR32, arith_shifted_reg32, mnemonic,
                           OpNode> {
    let Inst{31} = 0;
  }
  def Xrs : BaseAddSubSReg<isSub, 1, GPR64, arith_shifted_reg64, mnemonic,
                           OpNode> {
    let Inst{31} = 1;
  }

  // Add/Subtract extended register
  let AddedComplexity = 1 in {
  def Wrx : BaseAddSubEReg<isSub, 1, GPR32, GPR32sp,
                           arith_extended_reg32<i32>, mnemonic, OpNode> {
    let Inst{31} = 0;
  }
  def Xrx : BaseAddSubEReg<isSub, 1, GPR64, GPR64sp,
                           arith_extended_reg32<i64>, mnemonic, OpNode> {
    let Inst{31} = 1;
  }
  }

  def Xrx64 : BaseAddSubEReg64<isSub, 1, GPR64, GPR64sp, GPR64,
                               arith_extendlsl64, mnemonic> {
    // UXTX and SXTX only.
    let Inst{14-13} = 0b11;
    let Inst{31} = 1;
  }
  } // Defs = [NZCV]

  // Support negative immediates, e.g. adds Rd, Rn, -imm -> subs Rd, Rn, imm
  def : InstSubst<alias#"\t$Rd, $Rn, $imm",
                  (!cast<Instruction>(NAME # "Wri") GPR32:$Rd, GPR32sp:$Rn,
                      addsub_shifted_imm32_neg:$imm), 0>;
  def : InstSubst<alias#"\t$Rd, $Rn, $imm",
                  (!cast<Instruction>(NAME # "Xri") GPR64:$Rd, GPR64sp:$Rn,
                       addsub_shifted_imm64_neg:$imm), 0>;

  // Compare aliases
  def : InstAlias<cmp#"\t$src, $imm", (!cast<Instruction>(NAME#"Wri")
                  WZR, GPR32sp:$src, addsub_shifted_imm32:$imm), 5>;
  def : InstAlias<cmp#"\t$src, $imm", (!cast<Instruction>(NAME#"Xri")
                  XZR, GPR64sp:$src, addsub_shifted_imm64:$imm), 5>;
  def : InstAlias<cmp#"\t$src1, $src2$sh", (!cast<Instruction>(NAME#"Wrx")
                  WZR, GPR32sp:$src1, GPR32:$src2, arith_extend:$sh), 4>;
  def : InstAlias<cmp#"\t$src1, $src2$sh", (!cast<Instruction>(NAME#"Xrx")
                  XZR, GPR64sp:$src1, GPR32:$src2, arith_extend:$sh), 4>;
  def : InstAlias<cmp#"\t$src1, $src2$sh", (!cast<Instruction>(NAME#"Xrx64")
                  XZR, GPR64sp:$src1, GPR64:$src2, arith_extendlsl64:$sh), 4>;
  def : InstAlias<cmp#"\t$src1, $src2$sh", (!cast<Instruction>(NAME#"Wrs")
                  WZR, GPR32:$src1, GPR32:$src2, arith_shift32:$sh), 4>;
  def : InstAlias<cmp#"\t$src1, $src2$sh", (!cast<Instruction>(NAME#"Xrs")
                  XZR, GPR64:$src1, GPR64:$src2, arith_shift64:$sh), 4>;

  // Support negative immediates, e.g. cmp Rn, -imm -> cmn Rn, imm
  def : InstSubst<cmpAlias#"\t$src, $imm", (!cast<Instruction>(NAME#"Wri")
                  WZR, GPR32sp:$src, addsub_shifted_imm32_neg:$imm), 0>;
  def : InstSubst<cmpAlias#"\t$src, $imm", (!cast<Instruction>(NAME#"Xri")
                  XZR, GPR64sp:$src, addsub_shifted_imm64_neg:$imm), 0>;

  // Compare shorthands
  def : InstAlias<cmp#"\t$src1, $src2", (!cast<Instruction>(NAME#"Wrs")
                  WZR, GPR32:$src1, GPR32:$src2, 0), 5>;
  def : InstAlias<cmp#"\t$src1, $src2", (!cast<Instruction>(NAME#"Xrs")
                  XZR, GPR64:$src1, GPR64:$src2, 0), 5>;
  def : InstAlias<cmp#"\t$src1, $src2", (!cast<Instruction>(NAME#"Wrx")
                  WZR, GPR32sponly:$src1, GPR32:$src2, 16), 5>;
  def : InstAlias<cmp#"\t$src1, $src2", (!cast<Instruction>(NAME#"Xrx64")
                  XZR, GPR64sponly:$src1, GPR64:$src2, 24), 5>;

  // Register/register aliases with no shift when SP is not used.
  def : AddSubRegAlias<mnemonic, !cast<Instruction>(NAME#"Wrs"),
                       GPR32, GPR32, GPR32, 0>;
  def : AddSubRegAlias<mnemonic, !cast<Instruction>(NAME#"Xrs"),
                       GPR64, GPR64, GPR64, 0>;

  // Register/register aliases with no shift when the first source register
  // is SP.
  def : AddSubRegAlias<mnemonic, !cast<Instruction>(NAME#"Wrx"),
                       GPR32, GPR32sponly, GPR32, 16>; // UXTW #0
  def : AddSubRegAlias<mnemonic,
                       !cast<Instruction>(NAME#"Xrx64"),
                       GPR64, GPR64sponly, GPR64, 24>; // UXTX #0
}

class AddSubG<bit isSub, string asm_inst, SDPatternOperator OpNode>
      : BaseAddSubImm<
          isSub, 0, GPR64sp, asm_inst, "\t$Rd, $Rn, $imm6, $imm4",
          (ins GPR64sp:$Rn, uimm6s16:$imm6, imm0_15:$imm4),
          (set GPR64sp:$Rd, (OpNode GPR64sp:$Rn, imm0_63:$imm6, imm0_15:$imm4))> {
  bits<6> imm6;
  bits<4> imm4;
  let Inst{31} = 1;
  let Inst{23-22} = 0b10;
  let Inst{21-16} = imm6;
  let Inst{15-14} = 0b00;
  let Inst{13-10} = imm4;
  let Unpredictable{15-14} = 0b11;
}

class SUBP<bit setsFlags, string asm_instr, SDPatternOperator OpNode>
      : BaseTwoOperand<0b0000, GPR64, asm_instr, null_frag, GPR64sp, GPR64sp> {
  let Inst{31} = 1;
  let Inst{29} = setsFlags;
}

//---
// Extract
//---
def SDTA64EXTR : SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>,
                                      SDTCisPtrTy<3>]>;
def AArch64Extr : SDNode<"AArch64ISD::EXTR", SDTA64EXTR>;

class BaseExtractImm<RegisterClass regtype, Operand imm_type, string asm,
                     list<dag> patterns>
    : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm, imm_type:$imm),
         asm, "\t$Rd, $Rn, $Rm, $imm", "", patterns>,
      Sched<[WriteExtr, ReadExtrHi]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<6> imm;

  let Inst{30-23} = 0b00100111;
  let Inst{21}    = 0;
  let Inst{20-16} = Rm;
  let Inst{15-10} = imm;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass ExtractImm<string asm> {
  def Wrri : BaseExtractImm<GPR32, imm0_31, asm,
                      [(set GPR32:$Rd,
                        (AArch64Extr GPR32:$Rn, GPR32:$Rm, imm0_31:$imm))]> {
    let Inst{31} = 0;
    let Inst{22} = 0;
    // imm<5> must be zero.
    let imm{5}   = 0;
  }
  def Xrri : BaseExtractImm<GPR64, imm0_63, asm,
                      [(set GPR64:$Rd,
                        (AArch64Extr GPR64:$Rn, GPR64:$Rm, imm0_63:$imm))]> {

    let Inst{31} = 1;
    let Inst{22} = 1;
  }
}

//---
// Bitfield
//---

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseBitfieldImm<bits<2> opc,
                      RegisterClass regtype, Operand imm_type, string asm>
    : I<(outs regtype:$Rd), (ins regtype:$Rn, imm_type:$immr, imm_type:$imms),
         asm, "\t$Rd, $Rn, $immr, $imms", "", []>,
      Sched<[WriteIS, ReadI]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<6> immr;
  bits<6> imms;

  let Inst{30-29} = opc;
  let Inst{28-23} = 0b100110;
  let Inst{21-16} = immr;
  let Inst{15-10} = imms;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass BitfieldImm<bits<2> opc, string asm> {
  def Wri : BaseBitfieldImm<opc, GPR32, imm0_31, asm> {
    let Inst{31} = 0;
    let Inst{22} = 0;
    // imms<5> and immr<5> must be zero, else ReservedValue().
    let Inst{21} = 0;
    let Inst{15} = 0;
  }
  def Xri : BaseBitfieldImm<opc, GPR64, imm0_63, asm> {
    let Inst{31} = 1;
    let Inst{22} = 1;
  }
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseBitfieldImmWith2RegArgs<bits<2> opc,
                      RegisterClass regtype, Operand imm_type, string asm>
    : I<(outs regtype:$Rd), (ins regtype:$src, regtype:$Rn, imm_type:$immr,
                             imm_type:$imms),
         asm, "\t$Rd, $Rn, $immr, $imms", "$src = $Rd", []>,
      Sched<[WriteIS, ReadI]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<6> immr;
  bits<6> imms;

  let Inst{30-29} = opc;
  let Inst{28-23} = 0b100110;
  let Inst{21-16} = immr;
  let Inst{15-10} = imms;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass BitfieldImmWith2RegArgs<bits<2> opc, string asm> {
  def Wri : BaseBitfieldImmWith2RegArgs<opc, GPR32, imm0_31, asm> {
    let Inst{31} = 0;
    let Inst{22} = 0;
    // imms<5> and immr<5> must be zero, else ReservedValue().
    let Inst{21} = 0;
    let Inst{15} = 0;
  }
  def Xri : BaseBitfieldImmWith2RegArgs<opc, GPR64, imm0_63, asm> {
    let Inst{31} = 1;
    let Inst{22} = 1;
  }
}

//---
// Logical
//---

// Logical (immediate)
class BaseLogicalImm<bits<2> opc, RegisterClass dregtype,
                     RegisterClass sregtype, Operand imm_type, string asm,
                     list<dag> pattern>
    : I<(outs dregtype:$Rd), (ins sregtype:$Rn, imm_type:$imm),
         asm, "\t$Rd, $Rn, $imm", "", pattern>,
      Sched<[WriteI, ReadI]> {
  bits<5>  Rd;
  bits<5>  Rn;
  bits<13> imm;
  let Inst{30-29} = opc;
  let Inst{28-23} = 0b100100;
  let Inst{22}    = imm{12};
  let Inst{21-16} = imm{11-6};
  let Inst{15-10} = imm{5-0};
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;

  let DecoderMethod = "DecodeLogicalImmInstruction";
}

// Logical (shifted register)
class BaseLogicalSReg<bits<2> opc, bit N, RegisterClass regtype,
                      logical_shifted_reg shifted_regtype, string asm,
                      list<dag> pattern>
    : I<(outs regtype:$Rd), (ins regtype:$Rn, shifted_regtype:$Rm),
        asm, "\t$Rd, $Rn, $Rm", "", pattern>,
      Sched<[WriteISReg, ReadI, ReadISReg]> {
  // The operands are in order to match the 'addr' MI operands, so we
  // don't need an encoder method and by-name matching. Just use the default
  // in-order handling. Since we're using by-order, make sure the names
  // do not match.
  bits<5> dst;
  bits<5> src1;
  bits<5> src2;
  bits<8> shift;
  let Inst{30-29} = opc;
  let Inst{28-24} = 0b01010;
  let Inst{23-22} = shift{7-6};
  let Inst{21}    = N;
  let Inst{20-16} = src2;
  let Inst{15-10} = shift{5-0};
  let Inst{9-5}   = src1;
  let Inst{4-0}   = dst;

  let DecoderMethod = "DecodeThreeAddrSRegInstruction";
}

// Aliases for register+register logical instructions.
class LogicalRegAlias<string asm, Instruction inst, RegisterClass regtype>
    : InstAlias<asm#"\t$dst, $src1, $src2",
                (inst regtype:$dst, regtype:$src1, regtype:$src2, 0)>;

multiclass LogicalImm<bits<2> opc, string mnemonic, SDNode OpNode,
                      string Alias> {
  let AddedComplexity = 6, isReMaterializable = 1, isAsCheapAsAMove = 1 in
  def Wri : BaseLogicalImm<opc, GPR32sp, GPR32, logical_imm32, mnemonic,
                           [(set GPR32sp:$Rd, (OpNode GPR32:$Rn,
                                               logical_imm32:$imm))]> {
    let Inst{31} = 0;
    let Inst{22} = 0; // 64-bit version has an additional bit of immediate.
  }
  let AddedComplexity = 6, isReMaterializable = 1, isAsCheapAsAMove = 1 in
  def Xri : BaseLogicalImm<opc, GPR64sp, GPR64, logical_imm64, mnemonic,
                           [(set GPR64sp:$Rd, (OpNode GPR64:$Rn,
                                               logical_imm64:$imm))]> {
    let Inst{31} = 1;
  }

  def : InstSubst<Alias # "\t$Rd, $Rn, $imm",
                  (!cast<Instruction>(NAME # "Wri") GPR32sp:$Rd, GPR32:$Rn,
                      logical_imm32_not:$imm), 0>;
  def : InstSubst<Alias # "\t$Rd, $Rn, $imm",
                  (!cast<Instruction>(NAME # "Xri") GPR64sp:$Rd, GPR64:$Rn,
                       logical_imm64_not:$imm), 0>;
}

multiclass LogicalImmS<bits<2> opc, string mnemonic, SDNode OpNode,
                       string Alias> {
  let isCompare = 1, Defs = [NZCV] in {
  def Wri  : BaseLogicalImm<opc, GPR32, GPR32, logical_imm32, mnemonic,
      [(set GPR32:$Rd, (OpNode GPR32:$Rn, logical_imm32:$imm))]> {
    let Inst{31} = 0;
    let Inst{22} = 0; // 64-bit version has an additional bit of immediate.
  }
  def Xri  : BaseLogicalImm<opc, GPR64, GPR64, logical_imm64, mnemonic,
      [(set GPR64:$Rd, (OpNode GPR64:$Rn, logical_imm64:$imm))]> {
    let Inst{31} = 1;
  }
  } // end Defs = [NZCV]

  def : InstSubst<Alias # "\t$Rd, $Rn, $imm",
                  (!cast<Instruction>(NAME # "Wri") GPR32:$Rd, GPR32:$Rn,
                      logical_imm32_not:$imm), 0>;
  def : InstSubst<Alias # "\t$Rd, $Rn, $imm",
                  (!cast<Instruction>(NAME # "Xri") GPR64:$Rd, GPR64:$Rn,
                       logical_imm64_not:$imm), 0>;
}

class BaseLogicalRegPseudo<RegisterClass regtype, SDPatternOperator OpNode>
    : Pseudo<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm),
             [(set regtype:$Rd, (OpNode regtype:$Rn, regtype:$Rm))]>,
      Sched<[WriteI, ReadI, ReadI]>;

// Split from LogicalImm as not all instructions have both.
multiclass LogicalReg<bits<2> opc, bit N, string mnemonic,
                      SDPatternOperator OpNode> {
  let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
  def Wrr : BaseLogicalRegPseudo<GPR32, OpNode>;
  def Xrr : BaseLogicalRegPseudo<GPR64, OpNode>;
  }

  def Wrs : BaseLogicalSReg<opc, N, GPR32, logical_shifted_reg32, mnemonic,
                            [(set GPR32:$Rd, (OpNode GPR32:$Rn,
                                                 logical_shifted_reg32:$Rm))]> {
    let Inst{31} = 0;
  }
  def Xrs : BaseLogicalSReg<opc, N, GPR64, logical_shifted_reg64, mnemonic,
                            [(set GPR64:$Rd, (OpNode GPR64:$Rn,
                                                 logical_shifted_reg64:$Rm))]> {
    let Inst{31} = 1;
  }

  def : LogicalRegAlias<mnemonic,
                        !cast<Instruction>(NAME#"Wrs"), GPR32>;
  def : LogicalRegAlias<mnemonic,
                        !cast<Instruction>(NAME#"Xrs"), GPR64>;
}

// Split from LogicalReg to allow setting NZCV Defs
multiclass LogicalRegS<bits<2> opc, bit N, string mnemonic,
                       SDPatternOperator OpNode = null_frag> {
  let Defs = [NZCV], mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {
  def Wrr : BaseLogicalRegPseudo<GPR32, OpNode>;
  def Xrr : BaseLogicalRegPseudo<GPR64, OpNode>;

  def Wrs : BaseLogicalSReg<opc, N, GPR32, logical_shifted_reg32, mnemonic,
            [(set GPR32:$Rd, (OpNode GPR32:$Rn, logical_shifted_reg32:$Rm))]> {
    let Inst{31} = 0;
  }
  def Xrs : BaseLogicalSReg<opc, N, GPR64, logical_shifted_reg64, mnemonic,
            [(set GPR64:$Rd, (OpNode GPR64:$Rn, logical_shifted_reg64:$Rm))]> {
    let Inst{31} = 1;
  }
  } // Defs = [NZCV]

  def : LogicalRegAlias<mnemonic,
                        !cast<Instruction>(NAME#"Wrs"), GPR32>;
  def : LogicalRegAlias<mnemonic,
                        !cast<Instruction>(NAME#"Xrs"), GPR64>;
}

//---
// Conditionally set flags
//---

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseCondComparisonImm<bit op, RegisterClass regtype, ImmLeaf immtype,
                            string mnemonic, SDNode OpNode>
    : I<(outs), (ins regtype:$Rn, immtype:$imm, imm32_0_15:$nzcv, ccode:$cond),
         mnemonic, "\t$Rn, $imm, $nzcv, $cond", "",
         [(set NZCV, (OpNode regtype:$Rn, immtype:$imm, (i32 imm:$nzcv),
                             (i32 imm:$cond), NZCV))]>,
      Sched<[WriteI, ReadI]> {
  let Uses = [NZCV];
  let Defs = [NZCV];

  bits<5> Rn;
  bits<5> imm;
  bits<4> nzcv;
  bits<4> cond;

  let Inst{30}    = op;
  let Inst{29-21} = 0b111010010;
  let Inst{20-16} = imm;
  let Inst{15-12} = cond;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = nzcv;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseCondComparisonReg<bit op, RegisterClass regtype, string mnemonic,
                            SDNode OpNode>
    : I<(outs), (ins regtype:$Rn, regtype:$Rm, imm32_0_15:$nzcv, ccode:$cond),
         mnemonic, "\t$Rn, $Rm, $nzcv, $cond", "",
         [(set NZCV, (OpNode regtype:$Rn, regtype:$Rm, (i32 imm:$nzcv),
                             (i32 imm:$cond), NZCV))]>,
      Sched<[WriteI, ReadI, ReadI]> {
  let Uses = [NZCV];
  let Defs = [NZCV];

  bits<5> Rn;
  bits<5> Rm;
  bits<4> nzcv;
  bits<4> cond;

  let Inst{30}    = op;
  let Inst{29-21} = 0b111010010;
  let Inst{20-16} = Rm;
  let Inst{15-12} = cond;
  let Inst{11-10} = 0b00;
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = nzcv;
}

multiclass CondComparison<bit op, string mnemonic, SDNode OpNode> {
  // immediate operand variants
  def Wi : BaseCondComparisonImm<op, GPR32, imm32_0_31, mnemonic, OpNode> {
    let Inst{31} = 0;
  }
  def Xi : BaseCondComparisonImm<op, GPR64, imm0_31, mnemonic, OpNode> {
    let Inst{31} = 1;
  }
  // register operand variants
  def Wr : BaseCondComparisonReg<op, GPR32, mnemonic, OpNode> {
    let Inst{31} = 0;
  }
  def Xr : BaseCondComparisonReg<op, GPR64, mnemonic, OpNode> {
    let Inst{31} = 1;
  }
}

//---
// Conditional select
//---

class BaseCondSelect<bit op, bits<2> op2, RegisterClass regtype, string asm>
    : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm, ccode:$cond),
         asm, "\t$Rd, $Rn, $Rm, $cond", "",
         [(set regtype:$Rd,
               (AArch64csel regtype:$Rn, regtype:$Rm, (i32 imm:$cond), NZCV))]>,
      Sched<[WriteI, ReadI, ReadI]> {
  let Uses = [NZCV];

  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<4> cond;

  let Inst{30}    = op;
  let Inst{29-21} = 0b011010100;
  let Inst{20-16} = Rm;
  let Inst{15-12} = cond;
  let Inst{11-10} = op2;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass CondSelect<bit op, bits<2> op2, string asm> {
  def Wr : BaseCondSelect<op, op2, GPR32, asm> {
    let Inst{31} = 0;
  }
  def Xr : BaseCondSelect<op, op2, GPR64, asm> {
    let Inst{31} = 1;
  }
}

class BaseCondSelectOp<bit op, bits<2> op2, RegisterClass regtype, string asm,
                       PatFrag frag>
    : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm, ccode:$cond),
         asm, "\t$Rd, $Rn, $Rm, $cond", "",
         [(set regtype:$Rd,
               (AArch64csel regtype:$Rn, (frag regtype:$Rm),
               (i32 imm:$cond), NZCV))]>,
      Sched<[WriteI, ReadI, ReadI]> {
  let Uses = [NZCV];

  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<4> cond;

  let Inst{30}    = op;
  let Inst{29-21} = 0b011010100;
  let Inst{20-16} = Rm;
  let Inst{15-12} = cond;
  let Inst{11-10} = op2;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

def inv_cond_XFORM : SDNodeXForm<imm, [{
  AArch64CC::CondCode CC = static_cast<AArch64CC::CondCode>(N->getZExtValue());
  return CurDAG->getTargetConstant(AArch64CC::getInvertedCondCode(CC), SDLoc(N),
                                   MVT::i32);
}]>;

multiclass CondSelectOp<bit op, bits<2> op2, string asm, PatFrag frag> {
  def Wr : BaseCondSelectOp<op, op2, GPR32, asm, frag> {
    let Inst{31} = 0;
  }
  def Xr : BaseCondSelectOp<op, op2, GPR64, asm, frag> {
    let Inst{31} = 1;
  }

  def : Pat<(AArch64csel (frag GPR32:$Rm), GPR32:$Rn, (i32 imm:$cond), NZCV),
            (!cast<Instruction>(NAME # Wr) GPR32:$Rn, GPR32:$Rm,
                                           (inv_cond_XFORM imm:$cond))>;

  def : Pat<(AArch64csel (frag GPR64:$Rm), GPR64:$Rn, (i32 imm:$cond), NZCV),
            (!cast<Instruction>(NAME # Xr) GPR64:$Rn, GPR64:$Rm,
                                           (inv_cond_XFORM imm:$cond))>;
}

//---
// Special Mask Value
//---
def maski8_or_more : Operand<i32>,
  ImmLeaf<i32, [{ return (Imm & 0xff) == 0xff; }]> {
}
def maski16_or_more : Operand<i32>,
  ImmLeaf<i32, [{ return (Imm & 0xffff) == 0xffff; }]> {
}


//---
// Load/store
//---

// (unsigned immediate)
// Indexed for 8-bit registers. offset is in range [0,4095].
def am_indexed8 : ComplexPattern<i64, 2, "SelectAddrModeIndexed8", []>;
def am_indexed16 : ComplexPattern<i64, 2, "SelectAddrModeIndexed16", []>;
def am_indexed32 : ComplexPattern<i64, 2, "SelectAddrModeIndexed32", []>;
def am_indexed64 : ComplexPattern<i64, 2, "SelectAddrModeIndexed64", []>;
def am_indexed128 : ComplexPattern<i64, 2, "SelectAddrModeIndexed128", []>;

def gi_am_indexed8 :
    GIComplexOperandMatcher<s64, "selectAddrModeIndexed<8>">,
    GIComplexPatternEquiv<am_indexed8>;
def gi_am_indexed16 :
    GIComplexOperandMatcher<s64, "selectAddrModeIndexed<16>">,
    GIComplexPatternEquiv<am_indexed16>;
def gi_am_indexed32 :
    GIComplexOperandMatcher<s64, "selectAddrModeIndexed<32>">,
    GIComplexPatternEquiv<am_indexed32>;
def gi_am_indexed64 :
    GIComplexOperandMatcher<s64, "selectAddrModeIndexed<64>">,
    GIComplexPatternEquiv<am_indexed64>;
def gi_am_indexed128 :
    GIComplexOperandMatcher<s64, "selectAddrModeIndexed<128>">,
    GIComplexPatternEquiv<am_indexed128>;

class UImm12OffsetOperand<int Scale> : AsmOperandClass {
  let Name = "UImm12Offset" # Scale;
  let RenderMethod = "addUImm12OffsetOperands<" # Scale # ">";
  let PredicateMethod = "isUImm12Offset<" # Scale # ">";
  let DiagnosticType = "InvalidMemoryIndexed" # Scale;
}

def UImm12OffsetScale1Operand : UImm12OffsetOperand<1>;
def UImm12OffsetScale2Operand : UImm12OffsetOperand<2>;
def UImm12OffsetScale4Operand : UImm12OffsetOperand<4>;
def UImm12OffsetScale8Operand : UImm12OffsetOperand<8>;
def UImm12OffsetScale16Operand : UImm12OffsetOperand<16>;

class uimm12_scaled<int Scale> : Operand<i64> {
  let ParserMatchClass
   = !cast<AsmOperandClass>("UImm12OffsetScale" # Scale # "Operand");
  let EncoderMethod
   = "getLdStUImm12OpValue<AArch64::fixup_aarch64_ldst_imm12_scale" # Scale # ">";
  let PrintMethod = "printUImm12Offset<" # Scale # ">";
}

def uimm12s1 : uimm12_scaled<1>;
def uimm12s2 : uimm12_scaled<2>;
def uimm12s4 : uimm12_scaled<4>;
def uimm12s8 : uimm12_scaled<8>;
def uimm12s16 : uimm12_scaled<16>;

class BaseLoadStoreUI<bits<2> sz, bit V, bits<2> opc, dag oops, dag iops,
                      string asm, list<dag> pattern>
    : I<oops, iops, asm, "\t$Rt, [$Rn, $offset]", "", pattern> {
  bits<5> Rt;

  bits<5> Rn;
  bits<12> offset;

  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0b01;
  let Inst{23-22} = opc;
  let Inst{21-10} = offset;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;

  let DecoderMethod = "DecodeUnsignedLdStInstruction";
}

multiclass LoadUI<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                  Operand indextype, string asm, list<dag> pattern> {
  let AddedComplexity = 10, mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
  def ui : BaseLoadStoreUI<sz, V, opc, (outs regtype:$Rt),
                           (ins GPR64sp:$Rn, indextype:$offset),
                           asm, pattern>,
           Sched<[WriteLD]>;

  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "ui") regtype:$Rt, GPR64sp:$Rn, 0)>;
}

multiclass StoreUI<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
             Operand indextype, string asm, list<dag> pattern> {
  let AddedComplexity = 10, mayLoad = 0, mayStore = 1, hasSideEffects = 0 in
  def ui : BaseLoadStoreUI<sz, V, opc, (outs),
                           (ins regtype:$Rt, GPR64sp:$Rn, indextype:$offset),
                           asm, pattern>,
           Sched<[WriteST]>;

  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "ui") regtype:$Rt, GPR64sp:$Rn, 0)>;
}

// Same as StoreUI, but take a RegisterOperand. This is used by GlobalISel to
// substitute zero-registers automatically.
//
// TODO: Roll out zero-register subtitution to GPR32/GPR64 and fold this back
//       into StoreUI.
multiclass StoreUIz<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
             Operand indextype, string asm, list<dag> pattern> {
  let AddedComplexity = 10, mayLoad = 0, mayStore = 1, hasSideEffects = 0 in
  def ui : BaseLoadStoreUI<sz, V, opc, (outs),
                           (ins regtype:$Rt, GPR64sp:$Rn, indextype:$offset),
                           asm, pattern>,
           Sched<[WriteST]>;

  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "ui") regtype:$Rt, GPR64sp:$Rn, 0)>;
}

def PrefetchOperand : AsmOperandClass {
  let Name = "Prefetch";
  let ParserMethod = "tryParsePrefetch";
}
def prfop : Operand<i32> {
  let PrintMethod = "printPrefetchOp";
  let ParserMatchClass = PrefetchOperand;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 1 in
class PrefetchUI<bits<2> sz, bit V, bits<2> opc, string asm, list<dag> pat>
    : BaseLoadStoreUI<sz, V, opc,
                      (outs), (ins prfop:$Rt, GPR64sp:$Rn, uimm12s8:$offset),
                      asm, pat>,
      Sched<[WriteLD]>;

//---
// Load literal
//---

// Load literal address: 19-bit immediate. The low two bits of the target
// offset are implied zero and so are not part of the immediate.
def am_ldrlit : Operand<iPTR> {
  let EncoderMethod = "getLoadLiteralOpValue";
  let DecoderMethod = "DecodePCRelLabel19";
  let PrintMethod = "printAlignedLabel";
  let ParserMatchClass = PCRelLabel19Operand;
  let OperandType = "OPERAND_PCREL";
}

let mayLoad = 1, mayStore = 0, hasSideEffects = 0, AddedComplexity = 20 in
class LoadLiteral<bits<2> opc, bit V, RegisterOperand regtype, string asm, list<dag> pat>
    : I<(outs regtype:$Rt), (ins am_ldrlit:$label),
        asm, "\t$Rt, $label", "", pat>,
      Sched<[WriteLD]> {
  bits<5> Rt;
  bits<19> label;
  let Inst{31-30} = opc;
  let Inst{29-27} = 0b011;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-5}  = label;
  let Inst{4-0}   = Rt;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 1 in
class PrefetchLiteral<bits<2> opc, bit V, string asm, list<dag> pat>
    : I<(outs), (ins prfop:$Rt, am_ldrlit:$label),
        asm, "\t$Rt, $label", "", pat>,
      Sched<[WriteLD]> {
  bits<5> Rt;
  bits<19> label;
  let Inst{31-30} = opc;
  let Inst{29-27} = 0b011;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-5}  = label;
  let Inst{4-0}   = Rt;
}

//---
// Load/store register offset
//---

def ro_Xindexed8 : ComplexPattern<i64, 4, "SelectAddrModeXRO<8>", []>;
def ro_Xindexed16 : ComplexPattern<i64, 4, "SelectAddrModeXRO<16>", []>;
def ro_Xindexed32 : ComplexPattern<i64, 4, "SelectAddrModeXRO<32>", []>;
def ro_Xindexed64 : ComplexPattern<i64, 4, "SelectAddrModeXRO<64>", []>;
def ro_Xindexed128 : ComplexPattern<i64, 4, "SelectAddrModeXRO<128>", []>;

def ro_Windexed8 : ComplexPattern<i64, 4, "SelectAddrModeWRO<8>", []>;
def ro_Windexed16 : ComplexPattern<i64, 4, "SelectAddrModeWRO<16>", []>;
def ro_Windexed32 : ComplexPattern<i64, 4, "SelectAddrModeWRO<32>", []>;
def ro_Windexed64 : ComplexPattern<i64, 4, "SelectAddrModeWRO<64>", []>;
def ro_Windexed128 : ComplexPattern<i64, 4, "SelectAddrModeWRO<128>", []>;

class MemExtendOperand<string Reg, int Width> : AsmOperandClass {
  let Name = "Mem" # Reg # "Extend" # Width;
  let PredicateMethod = "isMem" # Reg # "Extend<" # Width # ">";
  let RenderMethod = "addMemExtendOperands";
  let DiagnosticType = "InvalidMemory" # Reg # "Extend" # Width;
}

def MemWExtend8Operand : MemExtendOperand<"W", 8> {
  // The address "[x0, x1, lsl #0]" actually maps to the variant which performs
  // the trivial shift.
  let RenderMethod = "addMemExtend8Operands";
}
def MemWExtend16Operand : MemExtendOperand<"W", 16>;
def MemWExtend32Operand : MemExtendOperand<"W", 32>;
def MemWExtend64Operand : MemExtendOperand<"W", 64>;
def MemWExtend128Operand : MemExtendOperand<"W", 128>;

def MemXExtend8Operand : MemExtendOperand<"X", 8> {
  // The address "[x0, x1, lsl #0]" actually maps to the variant which performs
  // the trivial shift.
  let RenderMethod = "addMemExtend8Operands";
}
def MemXExtend16Operand : MemExtendOperand<"X", 16>;
def MemXExtend32Operand : MemExtendOperand<"X", 32>;
def MemXExtend64Operand : MemExtendOperand<"X", 64>;
def MemXExtend128Operand : MemExtendOperand<"X", 128>;

class ro_extend<AsmOperandClass ParserClass, string Reg, int Width>
        : Operand<i32> {
  let ParserMatchClass = ParserClass;
  let PrintMethod = "printMemExtend<'" # Reg # "', " # Width # ">";
  let DecoderMethod = "DecodeMemExtend";
  let EncoderMethod = "getMemExtendOpValue";
  let MIOperandInfo = (ops i32imm:$signed, i32imm:$doshift);
}

def ro_Wextend8   : ro_extend<MemWExtend8Operand,   "w", 8>;
def ro_Wextend16  : ro_extend<MemWExtend16Operand,  "w", 16>;
def ro_Wextend32  : ro_extend<MemWExtend32Operand,  "w", 32>;
def ro_Wextend64  : ro_extend<MemWExtend64Operand,  "w", 64>;
def ro_Wextend128 : ro_extend<MemWExtend128Operand, "w", 128>;

def ro_Xextend8   : ro_extend<MemXExtend8Operand,   "x", 8>;
def ro_Xextend16  : ro_extend<MemXExtend16Operand,  "x", 16>;
def ro_Xextend32  : ro_extend<MemXExtend32Operand,  "x", 32>;
def ro_Xextend64  : ro_extend<MemXExtend64Operand,  "x", 64>;
def ro_Xextend128 : ro_extend<MemXExtend128Operand, "x", 128>;

class ROAddrMode<ComplexPattern windex, ComplexPattern xindex,
                  Operand wextend, Operand xextend>  {
  // CodeGen-level pattern covering the entire addressing mode.
  ComplexPattern Wpat = windex;
  ComplexPattern Xpat = xindex;

  // Asm-level Operand covering the valid "uxtw #3" style syntax.
  Operand Wext = wextend;
  Operand Xext = xextend;
}

def ro8 : ROAddrMode<ro_Windexed8, ro_Xindexed8, ro_Wextend8, ro_Xextend8>;
def ro16 : ROAddrMode<ro_Windexed16, ro_Xindexed16, ro_Wextend16, ro_Xextend16>;
def ro32 : ROAddrMode<ro_Windexed32, ro_Xindexed32, ro_Wextend32, ro_Xextend32>;
def ro64 : ROAddrMode<ro_Windexed64, ro_Xindexed64, ro_Wextend64, ro_Xextend64>;
def ro128 : ROAddrMode<ro_Windexed128, ro_Xindexed128, ro_Wextend128,
                       ro_Xextend128>;

class LoadStore8RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                      string asm, dag ins, dag outs, list<dag> pat>
    : I<ins, outs, asm, "\t$Rt, [$Rn, $Rm, $extend]", "", pat> {
  bits<5> Rt;
  bits<5> Rn;
  bits<5> Rm;
  bits<2> extend;
  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-22} = opc;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15}    = extend{1}; // sign extend Rm?
  let Inst{14}    = 1;
  let Inst{12}    = extend{0}; // do shift?
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;
}

class ROInstAlias<string asm, RegisterOperand regtype, Instruction INST>
  : InstAlias<asm # "\t$Rt, [$Rn, $Rm]",
              (INST regtype:$Rt, GPR64sp:$Rn, GPR64:$Rm, 0, 0)>;

multiclass Load8RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                   string asm, ValueType Ty, SDPatternOperator loadop> {
  let AddedComplexity = 10 in
  def roW : LoadStore8RO<sz, V, opc, regtype, asm,
                 (outs regtype:$Rt),
                 (ins GPR64sp:$Rn, GPR32:$Rm, ro_Wextend8:$extend),
                 [(set (Ty regtype:$Rt),
                       (loadop (ro_Windexed8 GPR64sp:$Rn, GPR32:$Rm,
                                             ro_Wextend8:$extend)))]>,
           Sched<[WriteLDIdx, ReadAdrBase]> {
    let Inst{13} = 0b0;
  }

  let AddedComplexity = 10 in
  def roX : LoadStore8RO<sz, V, opc, regtype, asm,
                 (outs regtype:$Rt),
                 (ins GPR64sp:$Rn, GPR64:$Rm, ro_Xextend8:$extend),
                 [(set (Ty regtype:$Rt),
                       (loadop (ro_Xindexed8 GPR64sp:$Rn, GPR64:$Rm,
                                             ro_Xextend8:$extend)))]>,
           Sched<[WriteLDIdx, ReadAdrBase]> {
    let Inst{13} = 0b1;
  }

  def : ROInstAlias<asm, regtype, !cast<Instruction>(NAME # "roX")>;
}

multiclass Store8RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                    string asm, ValueType Ty, SDPatternOperator storeop> {
  let AddedComplexity = 10 in
  def roW : LoadStore8RO<sz, V, opc, regtype, asm, (outs),
                 (ins regtype:$Rt, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend8:$extend),
                 [(storeop (Ty regtype:$Rt),
                           (ro_Windexed8 GPR64sp:$Rn, GPR32:$Rm,
                                         ro_Wextend8:$extend))]>,
            Sched<[WriteSTIdx, ReadAdrBase]> {
    let Inst{13} = 0b0;
  }

  let AddedComplexity = 10 in
  def roX : LoadStore8RO<sz, V, opc, regtype, asm, (outs),
                 (ins regtype:$Rt, GPR64sp:$Rn, GPR64:$Rm, ro_Xextend8:$extend),
                 [(storeop (Ty regtype:$Rt),
                           (ro_Xindexed8 GPR64sp:$Rn, GPR64:$Rm,
                                         ro_Xextend8:$extend))]>,
            Sched<[WriteSTIdx, ReadAdrBase]> {
    let Inst{13} = 0b1;
  }

  def : ROInstAlias<asm, regtype, !cast<Instruction>(NAME # "roX")>;
}

class LoadStore16RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                      string asm, dag ins, dag outs, list<dag> pat>
    : I<ins, outs, asm, "\t$Rt, [$Rn, $Rm, $extend]", "", pat> {
  bits<5> Rt;
  bits<5> Rn;
  bits<5> Rm;
  bits<2> extend;
  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-22} = opc;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15}    = extend{1}; // sign extend Rm?
  let Inst{14}    = 1;
  let Inst{12}    = extend{0}; // do shift?
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;
}

multiclass Load16RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                    string asm, ValueType Ty, SDPatternOperator loadop> {
  let AddedComplexity = 10 in
  def roW : LoadStore16RO<sz, V, opc, regtype, asm, (outs regtype:$Rt),
                 (ins GPR64sp:$Rn, GPR32:$Rm, ro_Wextend16:$extend),
                 [(set (Ty regtype:$Rt),
                       (loadop (ro_Windexed16 GPR64sp:$Rn, GPR32:$Rm,
                                              ro_Wextend16:$extend)))]>,
            Sched<[WriteLDIdx, ReadAdrBase]> {
    let Inst{13} = 0b0;
  }

  let AddedComplexity = 10 in
  def roX : LoadStore16RO<sz, V, opc, regtype, asm, (outs regtype:$Rt),
                 (ins GPR64sp:$Rn, GPR64:$Rm, ro_Xextend16:$extend),
                 [(set (Ty regtype:$Rt),
                       (loadop (ro_Xindexed16 GPR64sp:$Rn, GPR64:$Rm,
                                             ro_Xextend16:$extend)))]>,
            Sched<[WriteLDIdx, ReadAdrBase]> {
    let Inst{13} = 0b1;
  }

  def : ROInstAlias<asm, regtype, !cast<Instruction>(NAME # "roX")>;
}

multiclass Store16RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                     string asm, ValueType Ty, SDPatternOperator storeop> {
  let AddedComplexity = 10 in
  def roW : LoadStore16RO<sz, V, opc, regtype, asm, (outs),
                (ins regtype:$Rt, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend16:$extend),
                [(storeop (Ty regtype:$Rt),
                          (ro_Windexed16 GPR64sp:$Rn, GPR32:$Rm,
                                         ro_Wextend16:$extend))]>,
           Sched<[WriteSTIdx, ReadAdrBase]> {
    let Inst{13} = 0b0;
  }

  let AddedComplexity = 10 in
  def roX : LoadStore16RO<sz, V, opc, regtype, asm, (outs),
                (ins regtype:$Rt, GPR64sp:$Rn, GPR64:$Rm, ro_Xextend16:$extend),
                [(storeop (Ty regtype:$Rt),
                          (ro_Xindexed16 GPR64sp:$Rn, GPR64:$Rm,
                                         ro_Xextend16:$extend))]>,
           Sched<[WriteSTIdx, ReadAdrBase]> {
    let Inst{13} = 0b1;
  }

  def : ROInstAlias<asm, regtype, !cast<Instruction>(NAME # "roX")>;
}

class LoadStore32RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                      string asm, dag ins, dag outs, list<dag> pat>
    : I<ins, outs, asm, "\t$Rt, [$Rn, $Rm, $extend]", "", pat> {
  bits<5> Rt;
  bits<5> Rn;
  bits<5> Rm;
  bits<2> extend;
  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-22} = opc;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15}    = extend{1}; // sign extend Rm?
  let Inst{14}    = 1;
  let Inst{12}    = extend{0}; // do shift?
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;
}

multiclass Load32RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                    string asm, ValueType Ty, SDPatternOperator loadop> {
  let AddedComplexity = 10 in
  def roW : LoadStore32RO<sz, V, opc, regtype, asm, (outs regtype:$Rt),
                 (ins GPR64sp:$Rn, GPR32:$Rm, ro_Wextend32:$extend),
                 [(set (Ty regtype:$Rt),
                       (loadop (ro_Windexed32 GPR64sp:$Rn, GPR32:$Rm,
                                              ro_Wextend32:$extend)))]>,
           Sched<[WriteLDIdx, ReadAdrBase]> {
    let Inst{13} = 0b0;
  }

  let AddedComplexity = 10 in
  def roX : LoadStore32RO<sz, V, opc, regtype, asm, (outs regtype:$Rt),
                 (ins GPR64sp:$Rn, GPR64:$Rm, ro_Xextend32:$extend),
                 [(set (Ty regtype:$Rt),
                       (loadop (ro_Xindexed32 GPR64sp:$Rn, GPR64:$Rm,
                                              ro_Xextend32:$extend)))]>,
           Sched<[WriteLDIdx, ReadAdrBase]> {
    let Inst{13} = 0b1;
  }

  def : ROInstAlias<asm, regtype, !cast<Instruction>(NAME # "roX")>;
}

multiclass Store32RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                     string asm, ValueType Ty, SDPatternOperator storeop> {
  let AddedComplexity = 10 in
  def roW : LoadStore32RO<sz, V, opc, regtype, asm, (outs),
                (ins regtype:$Rt, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend32:$extend),
                [(storeop (Ty regtype:$Rt),
                          (ro_Windexed32 GPR64sp:$Rn, GPR32:$Rm,
                                         ro_Wextend32:$extend))]>,
            Sched<[WriteSTIdx, ReadAdrBase]> {
    let Inst{13} = 0b0;
  }

  let AddedComplexity = 10 in
  def roX : LoadStore32RO<sz, V, opc, regtype, asm, (outs),
                (ins regtype:$Rt, GPR64sp:$Rn, GPR64:$Rm, ro_Xextend32:$extend),
                [(storeop (Ty regtype:$Rt),
                          (ro_Xindexed32 GPR64sp:$Rn, GPR64:$Rm,
                                        ro_Xextend32:$extend))]>,
            Sched<[WriteSTIdx, ReadAdrBase]> {
    let Inst{13} = 0b1;
  }

  def : ROInstAlias<asm, regtype, !cast<Instruction>(NAME # "roX")>;
}

class LoadStore64RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                      string asm, dag ins, dag outs, list<dag> pat>
    : I<ins, outs, asm, "\t$Rt, [$Rn, $Rm, $extend]", "", pat> {
  bits<5> Rt;
  bits<5> Rn;
  bits<5> Rm;
  bits<2> extend;
  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-22} = opc;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15}    = extend{1}; // sign extend Rm?
  let Inst{14}    = 1;
  let Inst{12}    = extend{0}; // do shift?
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;
}

multiclass Load64RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                    string asm, ValueType Ty, SDPatternOperator loadop> {
  let AddedComplexity = 10, mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
  def roW : LoadStore64RO<sz, V, opc, regtype, asm, (outs regtype:$Rt),
                (ins GPR64sp:$Rn, GPR32:$Rm, ro_Wextend64:$extend),
                [(set (Ty regtype:$Rt),
                      (loadop (ro_Windexed64 GPR64sp:$Rn, GPR32:$Rm,
                                             ro_Wextend64:$extend)))]>,
           Sched<[WriteLDIdx, ReadAdrBase]> {
    let Inst{13} = 0b0;
  }

  let AddedComplexity = 10, mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
  def roX : LoadStore64RO<sz, V, opc, regtype, asm, (outs regtype:$Rt),
                (ins GPR64sp:$Rn, GPR64:$Rm, ro_Xextend64:$extend),
                 [(set (Ty regtype:$Rt),
                       (loadop (ro_Xindexed64 GPR64sp:$Rn, GPR64:$Rm,
                                              ro_Xextend64:$extend)))]>,
           Sched<[WriteLDIdx, ReadAdrBase]> {
    let Inst{13} = 0b1;
  }

  def : ROInstAlias<asm, regtype, !cast<Instruction>(NAME # "roX")>;
}

multiclass Store64RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                     string asm, ValueType Ty, SDPatternOperator storeop> {
  let AddedComplexity = 10, mayLoad = 0, mayStore = 1, hasSideEffects = 0 in
  def roW : LoadStore64RO<sz, V, opc, regtype, asm, (outs),
                (ins regtype:$Rt, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend64:$extend),
                [(storeop (Ty regtype:$Rt),
                          (ro_Windexed64 GPR64sp:$Rn, GPR32:$Rm,
                                         ro_Wextend64:$extend))]>,
            Sched<[WriteSTIdx, ReadAdrBase]> {
    let Inst{13} = 0b0;
  }

  let AddedComplexity = 10, mayLoad = 0, mayStore = 1, hasSideEffects = 0 in
  def roX : LoadStore64RO<sz, V, opc, regtype, asm, (outs),
                (ins regtype:$Rt, GPR64sp:$Rn, GPR64:$Rm, ro_Xextend64:$extend),
                [(storeop (Ty regtype:$Rt),
                          (ro_Xindexed64 GPR64sp:$Rn, GPR64:$Rm,
                                         ro_Xextend64:$extend))]>,
            Sched<[WriteSTIdx, ReadAdrBase]> {
    let Inst{13} = 0b1;
  }

  def : ROInstAlias<asm, regtype, !cast<Instruction>(NAME # "roX")>;
}

class LoadStore128RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                      string asm, dag ins, dag outs, list<dag> pat>
    : I<ins, outs, asm, "\t$Rt, [$Rn, $Rm, $extend]", "", pat> {
  bits<5> Rt;
  bits<5> Rn;
  bits<5> Rm;
  bits<2> extend;
  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-22} = opc;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15}    = extend{1}; // sign extend Rm?
  let Inst{14}    = 1;
  let Inst{12}    = extend{0}; // do shift?
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;
}

multiclass Load128RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                     string asm, ValueType Ty, SDPatternOperator loadop> {
  let AddedComplexity = 10, mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
  def roW : LoadStore128RO<sz, V, opc, regtype, asm, (outs regtype:$Rt),
                (ins GPR64sp:$Rn, GPR32:$Rm, ro_Wextend128:$extend),
                 [(set (Ty regtype:$Rt),
                       (loadop (ro_Windexed128 GPR64sp:$Rn, GPR32:$Rm,
                                               ro_Wextend128:$extend)))]>,
            Sched<[WriteLDIdx, ReadAdrBase]> {
    let Inst{13} = 0b0;
  }

  let AddedComplexity = 10, mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
  def roX : LoadStore128RO<sz, V, opc, regtype, asm, (outs regtype:$Rt),
                (ins GPR64sp:$Rn, GPR64:$Rm, ro_Xextend128:$extend),
                 [(set (Ty regtype:$Rt),
                       (loadop (ro_Xindexed128 GPR64sp:$Rn, GPR64:$Rm,
                                               ro_Xextend128:$extend)))]>,
            Sched<[WriteLDIdx, ReadAdrBase]> {
    let Inst{13} = 0b1;
  }

  def : ROInstAlias<asm, regtype, !cast<Instruction>(NAME # "roX")>;
}

multiclass Store128RO<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                      string asm, ValueType Ty, SDPatternOperator storeop> {
  let mayLoad = 0, mayStore = 1, hasSideEffects = 0 in
  def roW : LoadStore128RO<sz, V, opc, regtype, asm, (outs),
               (ins regtype:$Rt, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend128:$extend),
                []>,
            Sched<[WriteSTIdx, ReadAdrBase]> {
    let Inst{13} = 0b0;
  }

  let mayLoad = 0, mayStore = 1, hasSideEffects = 0 in
  def roX : LoadStore128RO<sz, V, opc, regtype, asm, (outs),
               (ins regtype:$Rt, GPR64sp:$Rn, GPR64:$Rm, ro_Xextend128:$extend),
                []>,
            Sched<[WriteSTIdx, ReadAdrBase]> {
    let Inst{13} = 0b1;
  }

  def : ROInstAlias<asm, regtype, !cast<Instruction>(NAME # "roX")>;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 1 in
class BasePrefetchRO<bits<2> sz, bit V, bits<2> opc, dag outs, dag ins,
                     string asm, list<dag> pat>
    : I<outs, ins, asm, "\t$Rt, [$Rn, $Rm, $extend]", "", pat>,
      Sched<[WriteLD]> {
  bits<5> Rt;
  bits<5> Rn;
  bits<5> Rm;
  bits<2> extend;
  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-22} = opc;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15}    = extend{1}; // sign extend Rm?
  let Inst{14}    = 1;
  let Inst{12}    = extend{0}; // do shift?
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;
}

multiclass PrefetchRO<bits<2> sz, bit V, bits<2> opc, string asm> {
  def roW : BasePrefetchRO<sz, V, opc, (outs),
                (ins prfop:$Rt, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend64:$extend),
                asm, [(AArch64Prefetch imm:$Rt,
                                     (ro_Windexed64 GPR64sp:$Rn, GPR32:$Rm,
                                                    ro_Wextend64:$extend))]> {
    let Inst{13} = 0b0;
  }

  def roX : BasePrefetchRO<sz, V, opc, (outs),
                (ins prfop:$Rt, GPR64sp:$Rn, GPR64:$Rm, ro_Xextend64:$extend),
                asm,  [(AArch64Prefetch imm:$Rt,
                                      (ro_Xindexed64 GPR64sp:$Rn, GPR64:$Rm,
                                                     ro_Xextend64:$extend))]> {
    let Inst{13} = 0b1;
  }

  def : InstAlias<"prfm $Rt, [$Rn, $Rm]",
               (!cast<Instruction>(NAME # "roX") prfop:$Rt,
                                                 GPR64sp:$Rn, GPR64:$Rm, 0, 0)>;
}

//---
// Load/store unscaled immediate
//---

def am_unscaled8 :  ComplexPattern<i64, 2, "SelectAddrModeUnscaled8", []>;
def am_unscaled16 : ComplexPattern<i64, 2, "SelectAddrModeUnscaled16", []>;
def am_unscaled32 : ComplexPattern<i64, 2, "SelectAddrModeUnscaled32", []>;
def am_unscaled64 : ComplexPattern<i64, 2, "SelectAddrModeUnscaled64", []>;
def am_unscaled128 :ComplexPattern<i64, 2, "SelectAddrModeUnscaled128", []>;

def gi_am_unscaled8 :
    GIComplexOperandMatcher<s64, "selectAddrModeUnscaled8">,
    GIComplexPatternEquiv<am_unscaled8>;
def gi_am_unscaled16 :
    GIComplexOperandMatcher<s64, "selectAddrModeUnscaled16">,
    GIComplexPatternEquiv<am_unscaled16>;
def gi_am_unscaled32 :
    GIComplexOperandMatcher<s64, "selectAddrModeUnscaled32">,
    GIComplexPatternEquiv<am_unscaled32>;
def gi_am_unscaled64 :
    GIComplexOperandMatcher<s64, "selectAddrModeUnscaled64">,
    GIComplexPatternEquiv<am_unscaled64>;
def gi_am_unscaled128 :
    GIComplexOperandMatcher<s64, "selectAddrModeUnscaled128">,
    GIComplexPatternEquiv<am_unscaled128>;


class BaseLoadStoreUnscale<bits<2> sz, bit V, bits<2> opc, dag oops, dag iops,
                           string asm, list<dag> pattern>
    : I<oops, iops, asm, "\t$Rt, [$Rn, $offset]", "", pattern> {
  bits<5> Rt;
  bits<5> Rn;
  bits<9> offset;
  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-22} = opc;
  let Inst{21}    = 0;
  let Inst{20-12} = offset;
  let Inst{11-10} = 0b00;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;

  let DecoderMethod = "DecodeSignedLdStInstruction";
}

// Armv8.4 LDAPR & STLR with Immediate Offset instruction
multiclass BaseLoadUnscaleV84<string asm, bits<2> sz, bits<2> opc,
                              RegisterOperand regtype > {
  def i : BaseLoadStoreUnscale<sz, 0, opc, (outs regtype:$Rt),
                               (ins GPR64sp:$Rn, simm9:$offset), asm, []>,
          Sched<[WriteST]> {
    let Inst{29} = 0;
    let Inst{24} = 1;
  }
  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "i") regtype:$Rt, GPR64sp:$Rn, 0)>;
}

multiclass BaseStoreUnscaleV84<string asm, bits<2> sz, bits<2> opc,
                               RegisterOperand regtype > {
  def i : BaseLoadStoreUnscale<sz, 0, opc, (outs),
                               (ins regtype:$Rt, GPR64sp:$Rn, simm9:$offset),
                               asm, []>,
          Sched<[WriteST]> {
    let Inst{29} = 0;
    let Inst{24} = 1;
  }
  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "i") regtype:$Rt, GPR64sp:$Rn, 0)>;
}

multiclass LoadUnscaled<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                   string asm, list<dag> pattern> {
  let AddedComplexity = 1 in // try this before LoadUI
  def i : BaseLoadStoreUnscale<sz, V, opc, (outs regtype:$Rt),
                               (ins GPR64sp:$Rn, simm9:$offset), asm, pattern>,
          Sched<[WriteLD]>;

  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "i") regtype:$Rt, GPR64sp:$Rn, 0)>;
}

multiclass StoreUnscaled<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                         string asm, list<dag> pattern> {
  let AddedComplexity = 1 in // try this before StoreUI
  def i : BaseLoadStoreUnscale<sz, V, opc, (outs),
                               (ins regtype:$Rt, GPR64sp:$Rn, simm9:$offset),
                               asm, pattern>,
          Sched<[WriteST]>;

  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "i") regtype:$Rt, GPR64sp:$Rn, 0)>;
}

multiclass PrefetchUnscaled<bits<2> sz, bit V, bits<2> opc, string asm,
                            list<dag> pat> {
  let mayLoad = 0, mayStore = 0, hasSideEffects = 1 in
  def i : BaseLoadStoreUnscale<sz, V, opc, (outs),
                               (ins prfop:$Rt, GPR64sp:$Rn, simm9:$offset),
                               asm, pat>,
          Sched<[WriteLD]>;

  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "i") prfop:$Rt, GPR64sp:$Rn, 0)>;
}

//---
// Load/store unscaled immediate, unprivileged
//---

class BaseLoadStoreUnprivileged<bits<2> sz, bit V, bits<2> opc,
                                dag oops, dag iops, string asm>
    : I<oops, iops, asm, "\t$Rt, [$Rn, $offset]", "", []> {
  bits<5> Rt;
  bits<5> Rn;
  bits<9> offset;
  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-22} = opc;
  let Inst{21}    = 0;
  let Inst{20-12} = offset;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;

  let DecoderMethod = "DecodeSignedLdStInstruction";
}

multiclass LoadUnprivileged<bits<2> sz, bit V, bits<2> opc,
                            RegisterClass regtype, string asm> {
  let mayStore = 0, mayLoad = 1, hasSideEffects = 0 in
  def i : BaseLoadStoreUnprivileged<sz, V, opc, (outs regtype:$Rt),
                                    (ins GPR64sp:$Rn, simm9:$offset), asm>,
          Sched<[WriteLD]>;

  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "i") regtype:$Rt, GPR64sp:$Rn, 0)>;
}

multiclass StoreUnprivileged<bits<2> sz, bit V, bits<2> opc,
                             RegisterClass regtype, string asm> {
  let mayStore = 1, mayLoad = 0, hasSideEffects = 0 in
  def i : BaseLoadStoreUnprivileged<sz, V, opc, (outs),
                                 (ins regtype:$Rt, GPR64sp:$Rn, simm9:$offset),
                                 asm>,
          Sched<[WriteST]>;

  def : InstAlias<asm # "\t$Rt, [$Rn]",
                  (!cast<Instruction>(NAME # "i") regtype:$Rt, GPR64sp:$Rn, 0)>;
}

//---
// Load/store pre-indexed
//---

class BaseLoadStorePreIdx<bits<2> sz, bit V, bits<2> opc, dag oops, dag iops,
                          string asm, string cstr, list<dag> pat>
    : I<oops, iops, asm, "\t$Rt, [$Rn, $offset]!", cstr, pat> {
  bits<5> Rt;
  bits<5> Rn;
  bits<9> offset;
  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0;
  let Inst{23-22} = opc;
  let Inst{21}    = 0;
  let Inst{20-12} = offset;
  let Inst{11-10} = 0b11;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;

  let DecoderMethod = "DecodeSignedLdStInstruction";
}

let hasSideEffects = 0 in {
let mayStore = 0, mayLoad = 1 in
class LoadPreIdx<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
             string asm>
    : BaseLoadStorePreIdx<sz, V, opc,
                     (outs GPR64sp:$wback, regtype:$Rt),
                     (ins GPR64sp:$Rn, simm9:$offset), asm,
                     "$Rn = $wback,@earlyclobber $wback", []>,
      Sched<[WriteLD, WriteAdr]>;

let mayStore = 1, mayLoad = 0 in
class StorePreIdx<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                  string asm, SDPatternOperator storeop, ValueType Ty>
    : BaseLoadStorePreIdx<sz, V, opc,
                      (outs GPR64sp:$wback),
                      (ins regtype:$Rt, GPR64sp:$Rn, simm9:$offset),
                      asm, "$Rn = $wback,@earlyclobber $wback",
      [(set GPR64sp:$wback,
            (storeop (Ty regtype:$Rt), GPR64sp:$Rn, simm9:$offset))]>,
      Sched<[WriteAdr, WriteST]>;
} // hasSideEffects = 0

//---
// Load/store post-indexed
//---

class BaseLoadStorePostIdx<bits<2> sz, bit V, bits<2> opc, dag oops, dag iops,
                          string asm, string cstr, list<dag> pat>
    : I<oops, iops, asm, "\t$Rt, [$Rn], $offset", cstr, pat> {
  bits<5> Rt;
  bits<5> Rn;
  bits<9> offset;
  let Inst{31-30} = sz;
  let Inst{29-27} = 0b111;
  let Inst{26}    = V;
  let Inst{25-24} = 0b00;
  let Inst{23-22} = opc;
  let Inst{21}    = 0b0;
  let Inst{20-12} = offset;
  let Inst{11-10} = 0b01;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;

  let DecoderMethod = "DecodeSignedLdStInstruction";
}

let hasSideEffects = 0 in {
let mayStore = 0, mayLoad = 1 in
class LoadPostIdx<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
             string asm>
    : BaseLoadStorePostIdx<sz, V, opc,
                      (outs GPR64sp:$wback, regtype:$Rt),
                      (ins GPR64sp:$Rn, simm9:$offset),
                      asm, "$Rn = $wback,@earlyclobber $wback", []>,
      Sched<[WriteLD, WriteAdr]>;

let mayStore = 1, mayLoad = 0 in
class StorePostIdx<bits<2> sz, bit V, bits<2> opc, RegisterOperand regtype,
                   string asm, SDPatternOperator storeop, ValueType Ty>
    : BaseLoadStorePostIdx<sz, V, opc,
                      (outs GPR64sp:$wback),
                      (ins regtype:$Rt, GPR64sp:$Rn, simm9:$offset),
                       asm, "$Rn = $wback,@earlyclobber $wback",
      [(set GPR64sp:$wback,
            (storeop (Ty regtype:$Rt), GPR64sp:$Rn, simm9:$offset))]>,
    Sched<[WriteAdr, WriteST]>;
} // hasSideEffects = 0


//---
// Load/store pair
//---

// (indexed, offset)

class BaseLoadStorePairOffset<bits<2> opc, bit V, bit L, dag oops, dag iops,
                              string asm>
    : I<oops, iops, asm, "\t$Rt, $Rt2, [$Rn, $offset]", "", []> {
  bits<5> Rt;
  bits<5> Rt2;
  bits<5> Rn;
  bits<7> offset;
  let Inst{31-30} = opc;
  let Inst{29-27} = 0b101;
  let Inst{26}    = V;
  let Inst{25-23} = 0b010;
  let Inst{22}    = L;
  let Inst{21-15} = offset;
  let Inst{14-10} = Rt2;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;

  let DecoderMethod = "DecodePairLdStInstruction";
}

multiclass LoadPairOffset<bits<2> opc, bit V, RegisterOperand regtype,
                          Operand indextype, string asm> {
  let hasSideEffects = 0, mayStore = 0, mayLoad = 1 in
  def i : BaseLoadStorePairOffset<opc, V, 1,
                                  (outs regtype:$Rt, regtype:$Rt2),
                                  (ins GPR64sp:$Rn, indextype:$offset), asm>,
          Sched<[WriteLD, WriteLDHi]>;

  def : InstAlias<asm # "\t$Rt, $Rt2, [$Rn]",
                  (!cast<Instruction>(NAME # "i") regtype:$Rt, regtype:$Rt2,
                                                  GPR64sp:$Rn, 0)>;
}


multiclass StorePairOffset<bits<2> opc, bit V, RegisterOperand regtype,
                           Operand indextype, string asm> {
  let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
  def i : BaseLoadStorePairOffset<opc, V, 0, (outs),
                                  (ins regtype:$Rt, regtype:$Rt2,
                                       GPR64sp:$Rn, indextype:$offset),
                                  asm>,
          Sched<[WriteSTP]>;

  def : InstAlias<asm # "\t$Rt, $Rt2, [$Rn]",
                  (!cast<Instruction>(NAME # "i") regtype:$Rt, regtype:$Rt2,
                                                  GPR64sp:$Rn, 0)>;
}

// (pre-indexed)
class BaseLoadStorePairPreIdx<bits<2> opc, bit V, bit L, dag oops, dag iops,
                              string asm>
    : I<oops, iops, asm, "\t$Rt, $Rt2, [$Rn, $offset]!", "$Rn = $wback,@earlyclobber $wback", []> {
  bits<5> Rt;
  bits<5> Rt2;
  bits<5> Rn;
  bits<7> offset;
  let Inst{31-30} = opc;
  let Inst{29-27} = 0b101;
  let Inst{26}    = V;
  let Inst{25-23} = 0b011;
  let Inst{22}    = L;
  let Inst{21-15} = offset;
  let Inst{14-10} = Rt2;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;

  let DecoderMethod = "DecodePairLdStInstruction";
}

let hasSideEffects = 0 in {
let mayStore = 0, mayLoad = 1 in
class LoadPairPreIdx<bits<2> opc, bit V, RegisterOperand regtype,
                     Operand indextype, string asm>
    : BaseLoadStorePairPreIdx<opc, V, 1,
                              (outs GPR64sp:$wback, regtype:$Rt, regtype:$Rt2),
                              (ins GPR64sp:$Rn, indextype:$offset), asm>,
      Sched<[WriteLD, WriteLDHi, WriteAdr]>;

let mayStore = 1, mayLoad = 0 in
class StorePairPreIdx<bits<2> opc, bit V, RegisterOperand regtype,
                      Operand indextype, string asm>
    : BaseLoadStorePairPreIdx<opc, V, 0, (outs GPR64sp:$wback),
                             (ins regtype:$Rt, regtype:$Rt2,
                                  GPR64sp:$Rn, indextype:$offset),
                             asm>,
      Sched<[WriteAdr, WriteSTP]>;
} // hasSideEffects = 0

// (post-indexed)

class BaseLoadStorePairPostIdx<bits<2> opc, bit V, bit L, dag oops, dag iops,
                              string asm>
    : I<oops, iops, asm, "\t$Rt, $Rt2, [$Rn], $offset", "$Rn = $wback,@earlyclobber $wback", []> {
  bits<5> Rt;
  bits<5> Rt2;
  bits<5> Rn;
  bits<7> offset;
  let Inst{31-30} = opc;
  let Inst{29-27} = 0b101;
  let Inst{26}    = V;
  let Inst{25-23} = 0b001;
  let Inst{22}    = L;
  let Inst{21-15} = offset;
  let Inst{14-10} = Rt2;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;

  let DecoderMethod = "DecodePairLdStInstruction";
}

let hasSideEffects = 0 in {
let mayStore = 0, mayLoad = 1 in
class LoadPairPostIdx<bits<2> opc, bit V, RegisterOperand regtype,
                      Operand idxtype, string asm>
    : BaseLoadStorePairPostIdx<opc, V, 1,
                              (outs GPR64sp:$wback, regtype:$Rt, regtype:$Rt2),
                              (ins GPR64sp:$Rn, idxtype:$offset), asm>,
      Sched<[WriteLD, WriteLDHi, WriteAdr]>;

let mayStore = 1, mayLoad = 0 in
class StorePairPostIdx<bits<2> opc, bit V, RegisterOperand regtype,
                       Operand idxtype, string asm>
    : BaseLoadStorePairPostIdx<opc, V, 0, (outs GPR64sp:$wback),
                             (ins regtype:$Rt, regtype:$Rt2,
                                  GPR64sp:$Rn, idxtype:$offset),
                             asm>,
      Sched<[WriteAdr, WriteSTP]>;
} // hasSideEffects = 0

//  (no-allocate)

class BaseLoadStorePairNoAlloc<bits<2> opc, bit V, bit L, dag oops, dag iops,
                              string asm>
    : I<oops, iops, asm, "\t$Rt, $Rt2, [$Rn, $offset]", "", []> {
  bits<5> Rt;
  bits<5> Rt2;
  bits<5> Rn;
  bits<7> offset;
  let Inst{31-30} = opc;
  let Inst{29-27} = 0b101;
  let Inst{26}    = V;
  let Inst{25-23} = 0b000;
  let Inst{22}    = L;
  let Inst{21-15} = offset;
  let Inst{14-10} = Rt2;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rt;

  let DecoderMethod = "DecodePairLdStInstruction";
}

multiclass LoadPairNoAlloc<bits<2> opc, bit V, RegisterClass regtype,
                           Operand indextype, string asm> {
  let hasSideEffects = 0, mayStore = 0, mayLoad = 1 in
  def i : BaseLoadStorePairNoAlloc<opc, V, 1,
                                   (outs regtype:$Rt, regtype:$Rt2),
                                   (ins GPR64sp:$Rn, indextype:$offset), asm>,
          Sched<[WriteLD, WriteLDHi]>;


  def : InstAlias<asm # "\t$Rt, $Rt2, [$Rn]",
                  (!cast<Instruction>(NAME # "i") regtype:$Rt, regtype:$Rt2,
                                                  GPR64sp:$Rn, 0)>;
}

multiclass StorePairNoAlloc<bits<2> opc, bit V, RegisterClass regtype,
                      Operand indextype, string asm> {
  let hasSideEffects = 0, mayStore = 1, mayLoad = 0 in
  def i : BaseLoadStorePairNoAlloc<opc, V, 0, (outs),
                                   (ins regtype:$Rt, regtype:$Rt2,
                                        GPR64sp:$Rn, indextype:$offset),
                                   asm>,
          Sched<[WriteSTP]>;

  def : InstAlias<asm # "\t$Rt, $Rt2, [$Rn]",
                  (!cast<Instruction>(NAME # "i") regtype:$Rt, regtype:$Rt2,
                                                  GPR64sp:$Rn, 0)>;
}

//---
// Load/store exclusive
//---

// True exclusive operations write to and/or read from the system's exclusive
// monitors, which as far as a compiler is concerned can be modelled as a
// random shared memory address. Hence LoadExclusive mayStore.
//
// Since these instructions have the undefined register bits set to 1 in
// their canonical form, we need a post encoder method to set those bits
// to 1 when encoding these instructions. We do this using the
// fixLoadStoreExclusive function. This function has template parameters:
//
// fixLoadStoreExclusive<int hasRs, int hasRt2>
//
// hasRs indicates that the instruction uses the Rs field, so we won't set
// it to 1 (and the same for Rt2). We don't need template parameters for
// the other register fields since Rt and Rn are always used.
//
let hasSideEffects = 1, mayLoad = 1, mayStore = 1 in
class BaseLoadStoreExclusive<bits<2> sz, bit o2, bit L, bit o1, bit o0,
                             dag oops, dag iops, string asm, string operands>
    : I<oops, iops, asm, operands, "", []> {
  let Inst{31-30} = sz;
  let Inst{29-24} = 0b001000;
  let Inst{23}    = o2;
  let Inst{22}    = L;
  let Inst{21}    = o1;
  let Inst{15}    = o0;

  let DecoderMethod = "DecodeExclusiveLdStInstruction";
}

// Neither Rs nor Rt2 operands.
class LoadStoreExclusiveSimple<bits<2> sz, bit o2, bit L, bit o1, bit o0,
                               dag oops, dag iops, string asm, string operands>
    : BaseLoadStoreExclusive<sz, o2, L, o1, o0, oops, iops, asm, operands> {
  bits<5> Rt;
  bits<5> Rn;
  let Inst{20-16} = 0b11111;
  let Unpredictable{20-16} = 0b11111;
  let Inst{14-10} = 0b11111;
  let Unpredictable{14-10} = 0b11111;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rt;

  let PostEncoderMethod = "fixLoadStoreExclusive<0,0>";
}

// Simple load acquires don't set the exclusive monitor
let mayLoad = 1, mayStore = 0 in
class LoadAcquire<bits<2> sz, bit o2, bit L, bit o1, bit o0,
                  RegisterClass regtype, string asm>
    : LoadStoreExclusiveSimple<sz, o2, L, o1, o0, (outs regtype:$Rt),
                               (ins GPR64sp0:$Rn), asm, "\t$Rt, [$Rn]">,
      Sched<[WriteLD]>;

class LoadExclusive<bits<2> sz, bit o2, bit L, bit o1, bit o0,
                    RegisterClass regtype, string asm>
    : LoadStoreExclusiveSimple<sz, o2, L, o1, o0, (outs regtype:$Rt),
                               (ins GPR64sp0:$Rn), asm, "\t$Rt, [$Rn]">,
      Sched<[WriteLD]>;

class LoadExclusivePair<bits<2> sz, bit o2, bit L, bit o1, bit o0,
                       RegisterClass regtype, string asm>
    : BaseLoadStoreExclusive<sz, o2, L, o1, o0,
                             (outs regtype:$Rt, regtype:$Rt2),
                             (ins GPR64sp0:$Rn), asm,
                             "\t$Rt, $Rt2, [$Rn]">,
      Sched<[WriteLD, WriteLDHi]> {
  bits<5> Rt;
  bits<5> Rt2;
  bits<5> Rn;
  let Inst{14-10} = Rt2;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rt;

  let PostEncoderMethod = "fixLoadStoreExclusive<0,1>";
}

// Simple store release operations do not check the exclusive monitor.
let mayLoad = 0, mayStore = 1 in
class StoreRelease<bits<2> sz, bit o2, bit L, bit o1, bit o0,
                   RegisterClass regtype, string asm>
    : LoadStoreExclusiveSimple<sz, o2, L, o1, o0, (outs),
                               (ins regtype:$Rt, GPR64sp0:$Rn),
                               asm, "\t$Rt, [$Rn]">,
      Sched<[WriteST]>;

let mayLoad = 1, mayStore = 1 in
class StoreExclusive<bits<2> sz, bit o2, bit L, bit o1, bit o0,
                     RegisterClass regtype, string asm>
    : BaseLoadStoreExclusive<sz, o2, L, o1, o0, (outs GPR32:$Ws),
                             (ins regtype:$Rt, GPR64sp0:$Rn),
                             asm, "\t$Ws, $Rt, [$Rn]">,
      Sched<[WriteSTX]> {
  bits<5> Ws;
  bits<5> Rt;
  bits<5> Rn;
  let Inst{20-16} = Ws;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rt;

  let Constraints = "@earlyclobber $Ws";
  let PostEncoderMethod = "fixLoadStoreExclusive<1,0>";
}

class StoreExclusivePair<bits<2> sz, bit o2, bit L, bit o1, bit o0,
                         RegisterClass regtype, string asm>
    : BaseLoadStoreExclusive<sz, o2, L, o1, o0,
                             (outs GPR32:$Ws),
                             (ins regtype:$Rt, regtype:$Rt2, GPR64sp0:$Rn),
                              asm, "\t$Ws, $Rt, $Rt2, [$Rn]">,
      Sched<[WriteSTX]> {
  bits<5> Ws;
  bits<5> Rt;
  bits<5> Rt2;
  bits<5> Rn;
  let Inst{20-16} = Ws;
  let Inst{14-10} = Rt2;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rt;

  let Constraints = "@earlyclobber $Ws";
}

// Armv8.5-A Memory Tagging Extension
class BaseMemTag<bits<2> opc1, bits<2> opc2, string asm_insn,
                 string asm_opnds, string cstr, dag oops, dag iops>
    : I<oops, iops, asm_insn, asm_opnds, cstr, []>,
      Sched<[]> {
  bits<5> Rn;

  let Inst{31-24} = 0b11011001;
  let Inst{23-22} = opc1;
  let Inst{21}    = 1;
  // Inst{20-12} defined by subclass
  let Inst{11-10} = opc2;
  let Inst{9-5}   = Rn;
  // Inst{4-0} defined by subclass
}

class MemTagVector<bit Load, string asm_insn, string asm_opnds,
                   dag oops, dag iops>
    : BaseMemTag<{0b1, Load}, 0b00, asm_insn, asm_opnds,
                 "$Rn = $wback,@earlyclobber $wback", oops, iops> {
  bits<5> Rt;

  let Inst{20-12} = 0b000000000;
  let Inst{4-0}   = Rt;

  let mayLoad = Load;
}

class MemTagLoad<string asm_insn, string asm_opnds>
    : BaseMemTag<0b01, 0b00, asm_insn, asm_opnds, "", (outs GPR64:$Rt),
                 (ins GPR64sp:$Rn, simm9s16:$offset)> {
  bits<5> Rt;
  bits<9> offset;

  let Inst{20-12} = offset;
  let Inst{4-0}   = Rt;

  let mayLoad = 1;
}

class BaseMemTagStore<bits<2> opc1, bits<2> opc2, string asm_insn,
                     string asm_opnds, string cstr, dag oops, dag iops>
    : BaseMemTag<opc1, opc2, asm_insn, asm_opnds, cstr, oops, iops> {
  bits<5> Rt;
  bits<9> offset;

  let Inst{20-12} = offset;
  let Inst{4-0}   = 0b11111;
  let Unpredictable{4-0} = 0b11111;

  let mayStore = 1;
}

multiclass MemTagStore<bits<2> opc1, string insn> {
  def Offset :
    BaseMemTagStore<opc1, 0b10, insn, "\t[$Rn, $offset]", "",
                    (outs), (ins GPR64sp:$Rn, simm9s16:$offset)>;
  def PreIndex :
    BaseMemTagStore<opc1, 0b11, insn, "\t[$Rn, $offset]!",
                    "$Rn = $wback,@earlyclobber $wback",
                    (outs GPR64sp:$wback),
                    (ins GPR64sp:$Rn, simm9s16:$offset)>;
  def PostIndex :
    BaseMemTagStore<opc1, 0b01, insn, "\t[$Rn], $offset",
                    "$Rn = $wback,@earlyclobber $wback",
                    (outs GPR64sp:$wback),
                    (ins GPR64sp:$Rn, simm9s16:$offset)>;

  def : InstAlias<insn # "\t[$Rn]",
                  (!cast<Instruction>(NAME # "Offset") GPR64sp:$Rn, 0)>;
}

//---
// Exception generation
//---

let mayLoad = 0, mayStore = 0, hasSideEffects = 1 in
class ExceptionGeneration<bits<3> op1, bits<2> ll, string asm>
    : I<(outs), (ins imm0_65535:$imm), asm, "\t$imm", "", []>,
      Sched<[WriteSys]> {
  bits<16> imm;
  let Inst{31-24} = 0b11010100;
  let Inst{23-21} = op1;
  let Inst{20-5}  = imm;
  let Inst{4-2}   = 0b000;
  let Inst{1-0}   = ll;
}

//---
// UDF : Permanently UNDEFINED instructions.  Format: Opc = 0x0000, 16 bit imm.
//--
let hasSideEffects = 1, isTrap = 1, mayLoad = 0, mayStore = 0 in {
class UDFType<bits<16> opc, string asm>
  : I<(outs), (ins uimm16:$imm),
       asm, "\t$imm", "", []>,
    Sched<[]> {
  bits<16> imm;
  let Inst{31-16} = opc;
  let Inst{15-0} = imm;
}
}
let Predicates = [HasFPARMv8] in {

//---
// Floating point to integer conversion
//---

class BaseFPToIntegerUnscaled<bits<2> type, bits<2> rmode, bits<3> opcode,
                      RegisterClass srcType, RegisterClass dstType,
                      string asm, list<dag> pattern>
    : I<(outs dstType:$Rd), (ins srcType:$Rn),
         asm, "\t$Rd, $Rn", "", pattern>,
      Sched<[WriteFCvt]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{30-29} = 0b00;
  let Inst{28-24} = 0b11110;
  let Inst{23-22} = type;
  let Inst{21}    = 1;
  let Inst{20-19} = rmode;
  let Inst{18-16} = opcode;
  let Inst{15-10} = 0;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseFPToInteger<bits<2> type, bits<2> rmode, bits<3> opcode,
                      RegisterClass srcType, RegisterClass dstType,
                      Operand immType, string asm, list<dag> pattern>
    : I<(outs dstType:$Rd), (ins srcType:$Rn, immType:$scale),
         asm, "\t$Rd, $Rn, $scale", "", pattern>,
      Sched<[WriteFCvt]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<6> scale;
  let Inst{30-29} = 0b00;
  let Inst{28-24} = 0b11110;
  let Inst{23-22} = type;
  let Inst{21}    = 0;
  let Inst{20-19} = rmode;
  let Inst{18-16} = opcode;
  let Inst{15-10} = scale;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass FPToIntegerUnscaled<bits<2> rmode, bits<3> opcode, string asm,
           SDPatternOperator OpN> {
  // Unscaled half-precision to 32-bit
  def UWHr : BaseFPToIntegerUnscaled<0b11, rmode, opcode, FPR16, GPR32, asm,
                                     [(set GPR32:$Rd, (OpN FPR16:$Rn))]> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Predicates = [HasFullFP16];
  }

  // Unscaled half-precision to 64-bit
  def UXHr : BaseFPToIntegerUnscaled<0b11, rmode, opcode, FPR16, GPR64, asm,
                                     [(set GPR64:$Rd, (OpN FPR16:$Rn))]> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Predicates = [HasFullFP16];
  }

  // Unscaled single-precision to 32-bit
  def UWSr : BaseFPToIntegerUnscaled<0b00, rmode, opcode, FPR32, GPR32, asm,
                                     [(set GPR32:$Rd, (OpN FPR32:$Rn))]> {
    let Inst{31} = 0; // 32-bit GPR flag
  }

  // Unscaled single-precision to 64-bit
  def UXSr : BaseFPToIntegerUnscaled<0b00, rmode, opcode, FPR32, GPR64, asm,
                                     [(set GPR64:$Rd, (OpN FPR32:$Rn))]> {
    let Inst{31} = 1; // 64-bit GPR flag
  }

  // Unscaled double-precision to 32-bit
  def UWDr : BaseFPToIntegerUnscaled<0b01, rmode, opcode, FPR64, GPR32, asm,
                                     [(set GPR32:$Rd, (OpN (f64 FPR64:$Rn)))]> {
    let Inst{31} = 0; // 32-bit GPR flag
  }

  // Unscaled double-precision to 64-bit
  def UXDr : BaseFPToIntegerUnscaled<0b01, rmode, opcode, FPR64, GPR64, asm,
                                     [(set GPR64:$Rd, (OpN (f64 FPR64:$Rn)))]> {
    let Inst{31} = 1; // 64-bit GPR flag
  }
}

multiclass FPToIntegerScaled<bits<2> rmode, bits<3> opcode, string asm,
                             SDPatternOperator OpN> {
  // Scaled half-precision to 32-bit
  def SWHri : BaseFPToInteger<0b11, rmode, opcode, FPR16, GPR32,
                              fixedpoint_f16_i32, asm,
              [(set GPR32:$Rd, (OpN (fmul FPR16:$Rn,
                                          fixedpoint_f16_i32:$scale)))]> {
    let Inst{31} = 0; // 32-bit GPR flag
    let scale{5} = 1;
    let Predicates = [HasFullFP16];
  }

  // Scaled half-precision to 64-bit
  def SXHri : BaseFPToInteger<0b11, rmode, opcode, FPR16, GPR64,
                              fixedpoint_f16_i64, asm,
              [(set GPR64:$Rd, (OpN (fmul FPR16:$Rn,
                                          fixedpoint_f16_i64:$scale)))]> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Predicates = [HasFullFP16];
  }

  // Scaled single-precision to 32-bit
  def SWSri : BaseFPToInteger<0b00, rmode, opcode, FPR32, GPR32,
                              fixedpoint_f32_i32, asm,
              [(set GPR32:$Rd, (OpN (fmul FPR32:$Rn,
                                          fixedpoint_f32_i32:$scale)))]> {
    let Inst{31} = 0; // 32-bit GPR flag
    let scale{5} = 1;
  }

  // Scaled single-precision to 64-bit
  def SXSri : BaseFPToInteger<0b00, rmode, opcode, FPR32, GPR64,
                              fixedpoint_f32_i64, asm,
              [(set GPR64:$Rd, (OpN (fmul FPR32:$Rn,
                                          fixedpoint_f32_i64:$scale)))]> {
    let Inst{31} = 1; // 64-bit GPR flag
  }

  // Scaled double-precision to 32-bit
  def SWDri : BaseFPToInteger<0b01, rmode, opcode, FPR64, GPR32,
                              fixedpoint_f64_i32, asm,
              [(set GPR32:$Rd, (OpN (fmul FPR64:$Rn,
                                          fixedpoint_f64_i32:$scale)))]> {
    let Inst{31} = 0; // 32-bit GPR flag
    let scale{5} = 1;
  }

  // Scaled double-precision to 64-bit
  def SXDri : BaseFPToInteger<0b01, rmode, opcode, FPR64, GPR64,
                              fixedpoint_f64_i64, asm,
              [(set GPR64:$Rd, (OpN (fmul FPR64:$Rn,
                                          fixedpoint_f64_i64:$scale)))]> {
    let Inst{31} = 1; // 64-bit GPR flag
  }
}

//---
// Integer to floating point conversion
//---

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
class BaseIntegerToFP<bit isUnsigned,
                      RegisterClass srcType, RegisterClass dstType,
                      Operand immType, string asm, list<dag> pattern>
    : I<(outs dstType:$Rd), (ins srcType:$Rn, immType:$scale),
         asm, "\t$Rd, $Rn, $scale", "", pattern>,
      Sched<[WriteFCvt]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<6> scale;
  let Inst{30-24} = 0b0011110;
  let Inst{21-17} = 0b00001;
  let Inst{16}    = isUnsigned;
  let Inst{15-10} = scale;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

class BaseIntegerToFPUnscaled<bit isUnsigned,
                      RegisterClass srcType, RegisterClass dstType,
                      ValueType dvt, string asm, SDNode node>
    : I<(outs dstType:$Rd), (ins srcType:$Rn),
         asm, "\t$Rd, $Rn", "", [(set (dvt dstType:$Rd), (node srcType:$Rn))]>,
      Sched<[WriteFCvt]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<6> scale;
  let Inst{30-24} = 0b0011110;
  let Inst{21-17} = 0b10001;
  let Inst{16}    = isUnsigned;
  let Inst{15-10} = 0b000000;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass IntegerToFP<bit isUnsigned, string asm, SDNode node> {
  // Unscaled
  def UWHri: BaseIntegerToFPUnscaled<isUnsigned, GPR32, FPR16, f16, asm, node> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Inst{23-22} = 0b11; // 16-bit FPR flag
    let Predicates = [HasFullFP16];
  }

  def UWSri: BaseIntegerToFPUnscaled<isUnsigned, GPR32, FPR32, f32, asm, node> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Inst{23-22} = 0b00; // 32-bit FPR flag
  }

  def UWDri: BaseIntegerToFPUnscaled<isUnsigned, GPR32, FPR64, f64, asm, node> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Inst{23-22} = 0b01; // 64-bit FPR flag
  }

  def UXHri: BaseIntegerToFPUnscaled<isUnsigned, GPR64, FPR16, f16, asm, node> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Inst{23-22} = 0b11; // 16-bit FPR flag
    let Predicates = [HasFullFP16];
  }

  def UXSri: BaseIntegerToFPUnscaled<isUnsigned, GPR64, FPR32, f32, asm, node> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Inst{23-22} = 0b00; // 32-bit FPR flag
  }

  def UXDri: BaseIntegerToFPUnscaled<isUnsigned, GPR64, FPR64, f64, asm, node> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Inst{23-22} = 0b01; // 64-bit FPR flag
  }

  // Scaled
  def SWHri: BaseIntegerToFP<isUnsigned, GPR32, FPR16, fixedpoint_f16_i32, asm,
                             [(set FPR16:$Rd,
                                   (fdiv (node GPR32:$Rn),
                                         fixedpoint_f16_i32:$scale))]> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Inst{23-22} = 0b11; // 16-bit FPR flag
    let scale{5} = 1;
    let Predicates = [HasFullFP16];
  }

  def SWSri: BaseIntegerToFP<isUnsigned, GPR32, FPR32, fixedpoint_f32_i32, asm,
                             [(set FPR32:$Rd,
                                   (fdiv (node GPR32:$Rn),
                                         fixedpoint_f32_i32:$scale))]> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Inst{23-22} = 0b00; // 32-bit FPR flag
    let scale{5} = 1;
  }

  def SWDri: BaseIntegerToFP<isUnsigned, GPR32, FPR64, fixedpoint_f64_i32, asm,
                             [(set FPR64:$Rd,
                                   (fdiv (node GPR32:$Rn),
                                         fixedpoint_f64_i32:$scale))]> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Inst{23-22} = 0b01; // 64-bit FPR flag
    let scale{5} = 1;
  }

  def SXHri: BaseIntegerToFP<isUnsigned, GPR64, FPR16, fixedpoint_f16_i64, asm,
                             [(set FPR16:$Rd,
                                   (fdiv (node GPR64:$Rn),
                                         fixedpoint_f16_i64:$scale))]> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Inst{23-22} = 0b11; // 16-bit FPR flag
    let Predicates = [HasFullFP16];
  }

  def SXSri: BaseIntegerToFP<isUnsigned, GPR64, FPR32, fixedpoint_f32_i64, asm,
                             [(set FPR32:$Rd,
                                   (fdiv (node GPR64:$Rn),
                                         fixedpoint_f32_i64:$scale))]> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Inst{23-22} = 0b00; // 32-bit FPR flag
  }

  def SXDri: BaseIntegerToFP<isUnsigned, GPR64, FPR64, fixedpoint_f64_i64, asm,
                             [(set FPR64:$Rd,
                                   (fdiv (node GPR64:$Rn),
                                         fixedpoint_f64_i64:$scale))]> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Inst{23-22} = 0b01; // 64-bit FPR flag
  }
}

//---
// Unscaled integer <-> floating point conversion (i.e. FMOV)
//---

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseUnscaledConversion<bits<2> rmode, bits<3> opcode,
                      RegisterClass srcType, RegisterClass dstType,
                      string asm>
    : I<(outs dstType:$Rd), (ins srcType:$Rn), asm, "\t$Rd, $Rn", "",
        // We use COPY_TO_REGCLASS for these bitconvert operations.
        // copyPhysReg() expands the resultant COPY instructions after
        // regalloc is done. This gives greater freedom for the allocator
        // and related passes (coalescing, copy propagation, et. al.) to
        // be more effective.
        [/*(set (dvt dstType:$Rd), (bitconvert (svt srcType:$Rn)))*/]>,
      Sched<[WriteFCopy]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{30-24} = 0b0011110;
  let Inst{21}    = 1;
  let Inst{20-19} = rmode;
  let Inst{18-16} = opcode;
  let Inst{15-10} = 0b000000;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseUnscaledConversionToHigh<bits<2> rmode, bits<3> opcode,
                     RegisterClass srcType, RegisterOperand dstType, string asm,
                     string kind>
    : I<(outs dstType:$Rd), (ins srcType:$Rn, VectorIndex1:$idx), asm,
        "{\t$Rd"#kind#"$idx, $Rn|"#kind#"\t$Rd$idx, $Rn}", "", []>,
      Sched<[WriteFCopy]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{30-23} = 0b00111101;
  let Inst{21}    = 1;
  let Inst{20-19} = rmode;
  let Inst{18-16} = opcode;
  let Inst{15-10} = 0b000000;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;

  let DecoderMethod =  "DecodeFMOVLaneInstruction";
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseUnscaledConversionFromHigh<bits<2> rmode, bits<3> opcode,
                     RegisterOperand srcType, RegisterClass dstType, string asm,
                     string kind>
    : I<(outs dstType:$Rd), (ins srcType:$Rn, VectorIndex1:$idx), asm,
        "{\t$Rd, $Rn"#kind#"$idx|"#kind#"\t$Rd, $Rn$idx}", "", []>,
      Sched<[WriteFCopy]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{30-23} = 0b00111101;
  let Inst{21}    = 1;
  let Inst{20-19} = rmode;
  let Inst{18-16} = opcode;
  let Inst{15-10} = 0b000000;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;

  let DecoderMethod =  "DecodeFMOVLaneInstruction";
}


multiclass UnscaledConversion<string asm> {
  def WHr : BaseUnscaledConversion<0b00, 0b111, GPR32, FPR16, asm> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Inst{23-22} = 0b11; // 16-bit FPR flag
    let Predicates = [HasFullFP16];
  }

  def XHr : BaseUnscaledConversion<0b00, 0b111, GPR64, FPR16, asm> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Inst{23-22} = 0b11; // 16-bit FPR flag
    let Predicates = [HasFullFP16];
  }

  def WSr : BaseUnscaledConversion<0b00, 0b111, GPR32, FPR32, asm> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Inst{23-22} = 0b00; // 32-bit FPR flag
  }

  def XDr : BaseUnscaledConversion<0b00, 0b111, GPR64, FPR64, asm> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Inst{23-22} = 0b01; // 64-bit FPR flag
  }

  def HWr : BaseUnscaledConversion<0b00, 0b110, FPR16, GPR32, asm> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Inst{23-22} = 0b11; // 16-bit FPR flag
    let Predicates = [HasFullFP16];
  }

  def HXr : BaseUnscaledConversion<0b00, 0b110, FPR16, GPR64, asm> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Inst{23-22} = 0b11; // 16-bit FPR flag
    let Predicates = [HasFullFP16];
  }

  def SWr : BaseUnscaledConversion<0b00, 0b110, FPR32, GPR32, asm> {
    let Inst{31} = 0; // 32-bit GPR flag
    let Inst{23-22} = 0b00; // 32-bit FPR flag
  }

  def DXr : BaseUnscaledConversion<0b00, 0b110, FPR64, GPR64, asm> {
    let Inst{31} = 1; // 64-bit GPR flag
    let Inst{23-22} = 0b01; // 64-bit FPR flag
  }

  def XDHighr : BaseUnscaledConversionToHigh<0b01, 0b111, GPR64, V128,
                                             asm, ".d"> {
    let Inst{31} = 1;
    let Inst{22} = 0;
  }

  def DXHighr : BaseUnscaledConversionFromHigh<0b01, 0b110, V128, GPR64,
                                               asm, ".d"> {
    let Inst{31} = 1;
    let Inst{22} = 0;
  }
}

//---
// Floating point conversion
//---

class BaseFPConversion<bits<2> type, bits<2> opcode, RegisterClass dstType,
                       RegisterClass srcType, string asm, list<dag> pattern>
    : I<(outs dstType:$Rd), (ins srcType:$Rn), asm, "\t$Rd, $Rn", "", pattern>,
      Sched<[WriteFCvt]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31-24} = 0b00011110;
  let Inst{23-22} = type;
  let Inst{21-17} = 0b10001;
  let Inst{16-15} = opcode;
  let Inst{14-10} = 0b10000;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass FPConversion<string asm> {
  // Double-precision to Half-precision
  def HDr : BaseFPConversion<0b01, 0b11, FPR16, FPR64, asm,
                             [(set FPR16:$Rd, (fpround FPR64:$Rn))]>;

  // Double-precision to Single-precision
  def SDr : BaseFPConversion<0b01, 0b00, FPR32, FPR64, asm,
                             [(set FPR32:$Rd, (fpround FPR64:$Rn))]>;

  // Half-precision to Double-precision
  def DHr : BaseFPConversion<0b11, 0b01, FPR64, FPR16, asm,
                             [(set FPR64:$Rd, (fpextend FPR16:$Rn))]>;

  // Half-precision to Single-precision
  def SHr : BaseFPConversion<0b11, 0b00, FPR32, FPR16, asm,
                             [(set FPR32:$Rd, (fpextend FPR16:$Rn))]>;

  // Single-precision to Double-precision
  def DSr : BaseFPConversion<0b00, 0b01, FPR64, FPR32, asm,
                             [(set FPR64:$Rd, (fpextend FPR32:$Rn))]>;

  // Single-precision to Half-precision
  def HSr : BaseFPConversion<0b00, 0b11, FPR16, FPR32, asm,
                             [(set FPR16:$Rd, (fpround FPR32:$Rn))]>;
}

//---
// Single operand floating point data processing
//---

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSingleOperandFPData<bits<6> opcode, RegisterClass regtype,
                              ValueType vt, string asm, SDPatternOperator node>
    : I<(outs regtype:$Rd), (ins regtype:$Rn), asm, "\t$Rd, $Rn", "",
         [(set (vt regtype:$Rd), (node (vt regtype:$Rn)))]>,
      Sched<[WriteF]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31-24} = 0b00011110;
  let Inst{21}    = 0b1;
  let Inst{20-15} = opcode;
  let Inst{14-10} = 0b10000;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SingleOperandFPData<bits<4> opcode, string asm,
                               SDPatternOperator node = null_frag> {

  def Hr : BaseSingleOperandFPData<{0b00,opcode}, FPR16, f16, asm, node> {
    let Inst{23-22} = 0b11; // 16-bit size flag
    let Predicates = [HasFullFP16];
  }

  def Sr : BaseSingleOperandFPData<{0b00,opcode}, FPR32, f32, asm, node> {
    let Inst{23-22} = 0b00; // 32-bit size flag
  }

  def Dr : BaseSingleOperandFPData<{0b00,opcode}, FPR64, f64, asm, node> {
    let Inst{23-22} = 0b01; // 64-bit size flag
  }
}

multiclass SingleOperandFPNo16<bits<6> opcode, string asm,
                  SDPatternOperator node = null_frag>{

  def Sr : BaseSingleOperandFPData<opcode, FPR32, f32, asm, node> {
    let Inst{23-22} = 0b00; // 32-bit registers
  }

  def Dr : BaseSingleOperandFPData<opcode, FPR64, f64, asm, node> {
    let Inst{23-22} = 0b01; // 64-bit registers
  }
}

// FRInt[32|64][Z|N] instructions
multiclass FRIntNNT<bits<2> opcode, string asm, SDPatternOperator node = null_frag> :
      SingleOperandFPNo16<{0b0100,opcode}, asm, node>;

//---
// Two operand floating point data processing
//---

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseTwoOperandFPData<bits<4> opcode, RegisterClass regtype,
                           string asm, list<dag> pat>
    : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm),
         asm, "\t$Rd, $Rn, $Rm", "", pat>,
      Sched<[WriteF]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31-24} = 0b00011110;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass TwoOperandFPData<bits<4> opcode, string asm,
                            SDPatternOperator node = null_frag> {
  def Hrr : BaseTwoOperandFPData<opcode, FPR16, asm,
                         [(set (f16 FPR16:$Rd),
                               (node (f16 FPR16:$Rn), (f16 FPR16:$Rm)))]> {
    let Inst{23-22} = 0b11; // 16-bit size flag
    let Predicates = [HasFullFP16];
  }

  def Srr : BaseTwoOperandFPData<opcode, FPR32, asm,
                         [(set (f32 FPR32:$Rd),
                               (node (f32 FPR32:$Rn), (f32 FPR32:$Rm)))]> {
    let Inst{23-22} = 0b00; // 32-bit size flag
  }

  def Drr : BaseTwoOperandFPData<opcode, FPR64, asm,
                         [(set (f64 FPR64:$Rd),
                               (node (f64 FPR64:$Rn), (f64 FPR64:$Rm)))]> {
    let Inst{23-22} = 0b01; // 64-bit size flag
  }
}

multiclass TwoOperandFPDataNeg<bits<4> opcode, string asm, SDNode node> {
  def Hrr : BaseTwoOperandFPData<opcode, FPR16, asm,
                  [(set FPR16:$Rd, (fneg (node FPR16:$Rn, (f16 FPR16:$Rm))))]> {
    let Inst{23-22} = 0b11; // 16-bit size flag
    let Predicates = [HasFullFP16];
  }

  def Srr : BaseTwoOperandFPData<opcode, FPR32, asm,
                  [(set FPR32:$Rd, (fneg (node FPR32:$Rn, (f32 FPR32:$Rm))))]> {
    let Inst{23-22} = 0b00; // 32-bit size flag
  }

  def Drr : BaseTwoOperandFPData<opcode, FPR64, asm,
                  [(set FPR64:$Rd, (fneg (node FPR64:$Rn, (f64 FPR64:$Rm))))]> {
    let Inst{23-22} = 0b01; // 64-bit size flag
  }
}


//---
// Three operand floating point data processing
//---

class BaseThreeOperandFPData<bit isNegated, bit isSub,
                             RegisterClass regtype, string asm, list<dag> pat>
    : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm, regtype: $Ra),
         asm, "\t$Rd, $Rn, $Rm, $Ra", "", pat>,
      Sched<[WriteFMul]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<5> Ra;
  let Inst{31-24} = 0b00011111;
  let Inst{21}    = isNegated;
  let Inst{20-16} = Rm;
  let Inst{15}    = isSub;
  let Inst{14-10} = Ra;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass ThreeOperandFPData<bit isNegated, bit isSub,string asm,
                              SDPatternOperator node> {
  def Hrrr : BaseThreeOperandFPData<isNegated, isSub, FPR16, asm,
            [(set FPR16:$Rd,
                  (node (f16 FPR16:$Rn), (f16 FPR16:$Rm), (f16 FPR16:$Ra)))]> {
    let Inst{23-22} = 0b11; // 16-bit size flag
    let Predicates = [HasFullFP16];
  }

  def Srrr : BaseThreeOperandFPData<isNegated, isSub, FPR32, asm,
            [(set FPR32:$Rd,
                  (node (f32 FPR32:$Rn), (f32 FPR32:$Rm), (f32 FPR32:$Ra)))]> {
    let Inst{23-22} = 0b00; // 32-bit size flag
  }

  def Drrr : BaseThreeOperandFPData<isNegated, isSub, FPR64, asm,
            [(set FPR64:$Rd,
                  (node (f64 FPR64:$Rn), (f64 FPR64:$Rm), (f64 FPR64:$Ra)))]> {
    let Inst{23-22} = 0b01; // 64-bit size flag
  }
}

//---
// Floating point data comparisons
//---

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseOneOperandFPComparison<bit signalAllNans,
                                 RegisterClass regtype, string asm,
                                 list<dag> pat>
    : I<(outs), (ins regtype:$Rn), asm, "\t$Rn, #0.0", "", pat>,
      Sched<[WriteFCmp]> {
  bits<5> Rn;
  let Inst{31-24} = 0b00011110;
  let Inst{21}    = 1;

  let Inst{15-10} = 0b001000;
  let Inst{9-5}   = Rn;
  let Inst{4}     = signalAllNans;
  let Inst{3-0}   = 0b1000;

  // Rm should be 0b00000 canonically, but we need to accept any value.
  let PostEncoderMethod = "fixOneOperandFPComparison";
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseTwoOperandFPComparison<bit signalAllNans, RegisterClass regtype,
                                string asm, list<dag> pat>
    : I<(outs), (ins regtype:$Rn, regtype:$Rm), asm, "\t$Rn, $Rm", "", pat>,
      Sched<[WriteFCmp]> {
  bits<5> Rm;
  bits<5> Rn;
  let Inst{31-24} = 0b00011110;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15-10} = 0b001000;
  let Inst{9-5}   = Rn;
  let Inst{4}     = signalAllNans;
  let Inst{3-0}   = 0b0000;
}

multiclass FPComparison<bit signalAllNans, string asm,
                        SDPatternOperator OpNode = null_frag> {
  let Defs = [NZCV] in {
  def Hrr : BaseTwoOperandFPComparison<signalAllNans, FPR16, asm,
      [(OpNode FPR16:$Rn, (f16 FPR16:$Rm)), (implicit NZCV)]> {
    let Inst{23-22} = 0b11;
    let Predicates = [HasFullFP16];
  }

  def Hri : BaseOneOperandFPComparison<signalAllNans, FPR16, asm,
      [(OpNode (f16 FPR16:$Rn), fpimm0), (implicit NZCV)]> {
    let Inst{23-22} = 0b11;
    let Predicates = [HasFullFP16];
  }

  def Srr : BaseTwoOperandFPComparison<signalAllNans, FPR32, asm,
      [(OpNode FPR32:$Rn, (f32 FPR32:$Rm)), (implicit NZCV)]> {
    let Inst{23-22} = 0b00;
  }

  def Sri : BaseOneOperandFPComparison<signalAllNans, FPR32, asm,
      [(OpNode (f32 FPR32:$Rn), fpimm0), (implicit NZCV)]> {
    let Inst{23-22} = 0b00;
  }

  def Drr : BaseTwoOperandFPComparison<signalAllNans, FPR64, asm,
      [(OpNode FPR64:$Rn, (f64 FPR64:$Rm)), (implicit NZCV)]> {
    let Inst{23-22} = 0b01;
  }

  def Dri : BaseOneOperandFPComparison<signalAllNans, FPR64, asm,
      [(OpNode (f64 FPR64:$Rn), fpimm0), (implicit NZCV)]> {
    let Inst{23-22} = 0b01;
  }
  } // Defs = [NZCV]
}

//---
// Floating point conditional comparisons
//---

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseFPCondComparison<bit signalAllNans, RegisterClass regtype,
                           string mnemonic, list<dag> pat>
    : I<(outs), (ins regtype:$Rn, regtype:$Rm, imm32_0_15:$nzcv, ccode:$cond),
         mnemonic, "\t$Rn, $Rm, $nzcv, $cond", "", pat>,
      Sched<[WriteFCmp]> {
  let Uses = [NZCV];
  let Defs = [NZCV];

  bits<5> Rn;
  bits<5> Rm;
  bits<4> nzcv;
  bits<4> cond;

  let Inst{31-24} = 0b00011110;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15-12} = cond;
  let Inst{11-10} = 0b01;
  let Inst{9-5}   = Rn;
  let Inst{4}     = signalAllNans;
  let Inst{3-0}   = nzcv;
}

multiclass FPCondComparison<bit signalAllNans, string mnemonic,
                            SDPatternOperator OpNode = null_frag> {
  def Hrr : BaseFPCondComparison<signalAllNans, FPR16, mnemonic,
      [(set NZCV, (OpNode (f16 FPR16:$Rn), (f16 FPR16:$Rm), (i32 imm:$nzcv),
                          (i32 imm:$cond), NZCV))]> {
    let Inst{23-22} = 0b11;
    let Predicates = [HasFullFP16];
  }

  def Srr : BaseFPCondComparison<signalAllNans, FPR32, mnemonic,
      [(set NZCV, (OpNode (f32 FPR32:$Rn), (f32 FPR32:$Rm), (i32 imm:$nzcv),
                          (i32 imm:$cond), NZCV))]> {
    let Inst{23-22} = 0b00;
  }

  def Drr : BaseFPCondComparison<signalAllNans, FPR64, mnemonic,
      [(set NZCV, (OpNode (f64 FPR64:$Rn), (f64 FPR64:$Rm), (i32 imm:$nzcv),
                          (i32 imm:$cond), NZCV))]> {
    let Inst{23-22} = 0b01;
  }
}

//---
// Floating point conditional select
//---

class BaseFPCondSelect<RegisterClass regtype, ValueType vt, string asm>
    : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm, ccode:$cond),
         asm, "\t$Rd, $Rn, $Rm, $cond", "",
         [(set regtype:$Rd,
               (AArch64csel (vt regtype:$Rn), regtype:$Rm,
                          (i32 imm:$cond), NZCV))]>,
      Sched<[WriteF]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<4> cond;

  let Inst{31-24} = 0b00011110;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15-12} = cond;
  let Inst{11-10} = 0b11;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass FPCondSelect<string asm> {
  let Uses = [NZCV] in {
  def Hrrr : BaseFPCondSelect<FPR16, f16, asm> {
    let Inst{23-22} = 0b11;
    let Predicates = [HasFullFP16];
  }

  def Srrr : BaseFPCondSelect<FPR32, f32, asm> {
    let Inst{23-22} = 0b00;
  }

  def Drrr : BaseFPCondSelect<FPR64, f64, asm> {
    let Inst{23-22} = 0b01;
  }
  } // Uses = [NZCV]
}

//---
// Floating move immediate
//---

class BaseFPMoveImmediate<RegisterClass regtype, Operand fpimmtype, string asm>
  : I<(outs regtype:$Rd), (ins fpimmtype:$imm), asm, "\t$Rd, $imm", "",
      [(set regtype:$Rd, fpimmtype:$imm)]>,
    Sched<[WriteFImm]> {
  bits<5> Rd;
  bits<8> imm;
  let Inst{31-24} = 0b00011110;
  let Inst{21}    = 1;
  let Inst{20-13} = imm;
  let Inst{12-5}  = 0b10000000;
  let Inst{4-0}   = Rd;
}

multiclass FPMoveImmediate<string asm> {
  def Hi : BaseFPMoveImmediate<FPR16, fpimm16, asm> {
    let Inst{23-22} = 0b11;
    let Predicates = [HasFullFP16];
  }

  def Si : BaseFPMoveImmediate<FPR32, fpimm32, asm> {
    let Inst{23-22} = 0b00;
  }

  def Di : BaseFPMoveImmediate<FPR64, fpimm64, asm> {
    let Inst{23-22} = 0b01;
  }
}
} // end of 'let Predicates = [HasFPARMv8]'

//----------------------------------------------------------------------------
// AdvSIMD
//----------------------------------------------------------------------------

let Predicates = [HasNEON] in {

//----------------------------------------------------------------------------
// AdvSIMD three register vector instructions
//----------------------------------------------------------------------------

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDThreeSameVector<bit Q, bit U, bits<3> size, bits<5> opcode,
                        RegisterOperand regtype, string asm, string kind,
                        list<dag> pattern>
  : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm), asm,
      "{\t$Rd" # kind # ", $Rn" # kind # ", $Rm" # kind #
      "|" # kind # "\t$Rd, $Rn, $Rm|}", "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-21} = size;
  let Inst{20-16} = Rm;
  let Inst{15-11} = opcode;
  let Inst{10}    = 1;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDThreeSameVectorTied<bit Q, bit U, bits<3> size, bits<5> opcode,
                        RegisterOperand regtype, string asm, string kind,
                        list<dag> pattern>
  : I<(outs regtype:$dst), (ins regtype:$Rd, regtype:$Rn, regtype:$Rm), asm,
      "{\t$Rd" # kind # ", $Rn" # kind # ", $Rm" # kind #
      "|" # kind # "\t$Rd, $Rn, $Rm}", "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-21} = size;
  let Inst{20-16} = Rm;
  let Inst{15-11} = opcode;
  let Inst{10}    = 1;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

// All operand sizes distinguished in the encoding.
multiclass SIMDThreeSameVector<bit U, bits<5> opc, string asm,
                               SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDThreeSameVector<0, U, 0b001, opc, V64,
                                      asm, ".8b",
         [(set (v8i8 V64:$Rd), (OpNode (v8i8 V64:$Rn), (v8i8 V64:$Rm)))]>;
  def v16i8 : BaseSIMDThreeSameVector<1, U, 0b001, opc, V128,
                                      asm, ".16b",
         [(set (v16i8 V128:$Rd), (OpNode (v16i8 V128:$Rn), (v16i8 V128:$Rm)))]>;
  def v4i16 : BaseSIMDThreeSameVector<0, U, 0b011, opc, V64,
                                      asm, ".4h",
         [(set (v4i16 V64:$Rd), (OpNode (v4i16 V64:$Rn), (v4i16 V64:$Rm)))]>;
  def v8i16 : BaseSIMDThreeSameVector<1, U, 0b011, opc, V128,
                                      asm, ".8h",
         [(set (v8i16 V128:$Rd), (OpNode (v8i16 V128:$Rn), (v8i16 V128:$Rm)))]>;
  def v2i32 : BaseSIMDThreeSameVector<0, U, 0b101, opc, V64,
                                      asm, ".2s",
         [(set (v2i32 V64:$Rd), (OpNode (v2i32 V64:$Rn), (v2i32 V64:$Rm)))]>;
  def v4i32 : BaseSIMDThreeSameVector<1, U, 0b101, opc, V128,
                                      asm, ".4s",
         [(set (v4i32 V128:$Rd), (OpNode (v4i32 V128:$Rn), (v4i32 V128:$Rm)))]>;
  def v2i64 : BaseSIMDThreeSameVector<1, U, 0b111, opc, V128,
                                      asm, ".2d",
         [(set (v2i64 V128:$Rd), (OpNode (v2i64 V128:$Rn), (v2i64 V128:$Rm)))]>;
}

// As above, but D sized elements unsupported.
multiclass SIMDThreeSameVectorBHS<bit U, bits<5> opc, string asm,
                                  SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDThreeSameVector<0, U, 0b001, opc, V64,
                                      asm, ".8b",
        [(set V64:$Rd, (v8i8 (OpNode (v8i8 V64:$Rn), (v8i8 V64:$Rm))))]>;
  def v16i8 : BaseSIMDThreeSameVector<1, U, 0b001, opc, V128,
                                      asm, ".16b",
        [(set V128:$Rd, (v16i8 (OpNode (v16i8 V128:$Rn), (v16i8 V128:$Rm))))]>;
  def v4i16 : BaseSIMDThreeSameVector<0, U, 0b011, opc, V64,
                                      asm, ".4h",
        [(set V64:$Rd, (v4i16 (OpNode (v4i16 V64:$Rn), (v4i16 V64:$Rm))))]>;
  def v8i16 : BaseSIMDThreeSameVector<1, U, 0b011, opc, V128,
                                      asm, ".8h",
        [(set V128:$Rd, (v8i16 (OpNode (v8i16 V128:$Rn), (v8i16 V128:$Rm))))]>;
  def v2i32 : BaseSIMDThreeSameVector<0, U, 0b101, opc, V64,
                                      asm, ".2s",
        [(set V64:$Rd, (v2i32 (OpNode (v2i32 V64:$Rn), (v2i32 V64:$Rm))))]>;
  def v4i32 : BaseSIMDThreeSameVector<1, U, 0b101, opc, V128,
                                      asm, ".4s",
        [(set V128:$Rd, (v4i32 (OpNode (v4i32 V128:$Rn), (v4i32 V128:$Rm))))]>;
}

multiclass SIMDThreeSameVectorBHSTied<bit U, bits<5> opc, string asm,
                                  SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDThreeSameVectorTied<0, U, 0b001, opc, V64,
                                      asm, ".8b",
      [(set (v8i8 V64:$dst),
            (OpNode (v8i8 V64:$Rd), (v8i8 V64:$Rn), (v8i8 V64:$Rm)))]>;
  def v16i8 : BaseSIMDThreeSameVectorTied<1, U, 0b001, opc, V128,
                                      asm, ".16b",
      [(set (v16i8 V128:$dst),
            (OpNode (v16i8 V128:$Rd), (v16i8 V128:$Rn), (v16i8 V128:$Rm)))]>;
  def v4i16 : BaseSIMDThreeSameVectorTied<0, U, 0b011, opc, V64,
                                      asm, ".4h",
      [(set (v4i16 V64:$dst),
            (OpNode (v4i16 V64:$Rd), (v4i16 V64:$Rn), (v4i16 V64:$Rm)))]>;
  def v8i16 : BaseSIMDThreeSameVectorTied<1, U, 0b011, opc, V128,
                                      asm, ".8h",
      [(set (v8i16 V128:$dst),
            (OpNode (v8i16 V128:$Rd), (v8i16 V128:$Rn), (v8i16 V128:$Rm)))]>;
  def v2i32 : BaseSIMDThreeSameVectorTied<0, U, 0b101, opc, V64,
                                      asm, ".2s",
      [(set (v2i32 V64:$dst),
            (OpNode (v2i32 V64:$Rd), (v2i32 V64:$Rn), (v2i32 V64:$Rm)))]>;
  def v4i32 : BaseSIMDThreeSameVectorTied<1, U, 0b101, opc, V128,
                                      asm, ".4s",
      [(set (v4i32 V128:$dst),
            (OpNode (v4i32 V128:$Rd), (v4i32 V128:$Rn), (v4i32 V128:$Rm)))]>;
}

// As above, but only B sized elements supported.
multiclass SIMDThreeSameVectorB<bit U, bits<5> opc, string asm,
                                SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDThreeSameVector<0, U, 0b001, opc, V64,
                                      asm, ".8b",
    [(set (v8i8 V64:$Rd), (OpNode (v8i8 V64:$Rn), (v8i8 V64:$Rm)))]>;
  def v16i8 : BaseSIMDThreeSameVector<1, U, 0b001, opc, V128,
                                      asm, ".16b",
    [(set (v16i8 V128:$Rd),
          (OpNode (v16i8 V128:$Rn), (v16i8 V128:$Rm)))]>;
}

// As above, but only floating point elements supported.
multiclass SIMDThreeSameVectorFP<bit U, bit S, bits<3> opc,
                                 string asm, SDPatternOperator OpNode> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4f16 : BaseSIMDThreeSameVector<0, U, {S,0b10}, {0b00,opc}, V64,
                                      asm, ".4h",
        [(set (v4f16 V64:$Rd), (OpNode (v4f16 V64:$Rn), (v4f16 V64:$Rm)))]>;
  def v8f16 : BaseSIMDThreeSameVector<1, U, {S,0b10}, {0b00,opc}, V128,
                                      asm, ".8h",
        [(set (v8f16 V128:$Rd), (OpNode (v8f16 V128:$Rn), (v8f16 V128:$Rm)))]>;
  } // Predicates = [HasNEON, HasFullFP16]
  def v2f32 : BaseSIMDThreeSameVector<0, U, {S,0b01}, {0b11,opc}, V64,
                                      asm, ".2s",
        [(set (v2f32 V64:$Rd), (OpNode (v2f32 V64:$Rn), (v2f32 V64:$Rm)))]>;
  def v4f32 : BaseSIMDThreeSameVector<1, U, {S,0b01}, {0b11,opc}, V128,
                                      asm, ".4s",
        [(set (v4f32 V128:$Rd), (OpNode (v4f32 V128:$Rn), (v4f32 V128:$Rm)))]>;
  def v2f64 : BaseSIMDThreeSameVector<1, U, {S,0b11}, {0b11,opc}, V128,
                                      asm, ".2d",
        [(set (v2f64 V128:$Rd), (OpNode (v2f64 V128:$Rn), (v2f64 V128:$Rm)))]>;
}

multiclass SIMDThreeSameVectorFPCmp<bit U, bit S, bits<3> opc,
                                    string asm,
                                    SDPatternOperator OpNode> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4f16 : BaseSIMDThreeSameVector<0, U, {S,0b10}, {0b00,opc}, V64,
                                      asm, ".4h",
        [(set (v4i16 V64:$Rd), (OpNode (v4f16 V64:$Rn), (v4f16 V64:$Rm)))]>;
  def v8f16 : BaseSIMDThreeSameVector<1, U, {S,0b10}, {0b00,opc}, V128,
                                      asm, ".8h",
        [(set (v8i16 V128:$Rd), (OpNode (v8f16 V128:$Rn), (v8f16 V128:$Rm)))]>;
  } // Predicates = [HasNEON, HasFullFP16]
  def v2f32 : BaseSIMDThreeSameVector<0, U, {S,0b01}, {0b11,opc}, V64,
                                      asm, ".2s",
        [(set (v2i32 V64:$Rd), (OpNode (v2f32 V64:$Rn), (v2f32 V64:$Rm)))]>;
  def v4f32 : BaseSIMDThreeSameVector<1, U, {S,0b01}, {0b11,opc}, V128,
                                      asm, ".4s",
        [(set (v4i32 V128:$Rd), (OpNode (v4f32 V128:$Rn), (v4f32 V128:$Rm)))]>;
  def v2f64 : BaseSIMDThreeSameVector<1, U, {S,0b11}, {0b11,opc}, V128,
                                      asm, ".2d",
        [(set (v2i64 V128:$Rd), (OpNode (v2f64 V128:$Rn), (v2f64 V128:$Rm)))]>;
}

multiclass SIMDThreeSameVectorFPTied<bit U, bit S, bits<3> opc,
                                 string asm, SDPatternOperator OpNode> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4f16 : BaseSIMDThreeSameVectorTied<0, U, {S,0b10}, {0b00,opc}, V64,
                                      asm, ".4h",
     [(set (v4f16 V64:$dst),
           (OpNode (v4f16 V64:$Rd), (v4f16 V64:$Rn), (v4f16 V64:$Rm)))]>;
  def v8f16 : BaseSIMDThreeSameVectorTied<1, U, {S,0b10}, {0b00,opc}, V128,
                                      asm, ".8h",
     [(set (v8f16 V128:$dst),
           (OpNode (v8f16 V128:$Rd), (v8f16 V128:$Rn), (v8f16 V128:$Rm)))]>;
  } // Predicates = [HasNEON, HasFullFP16]
  def v2f32 : BaseSIMDThreeSameVectorTied<0, U, {S,0b01}, {0b11,opc}, V64,
                                      asm, ".2s",
     [(set (v2f32 V64:$dst),
           (OpNode (v2f32 V64:$Rd), (v2f32 V64:$Rn), (v2f32 V64:$Rm)))]>;
  def v4f32 : BaseSIMDThreeSameVectorTied<1, U, {S,0b01}, {0b11,opc}, V128,
                                      asm, ".4s",
     [(set (v4f32 V128:$dst),
           (OpNode (v4f32 V128:$Rd), (v4f32 V128:$Rn), (v4f32 V128:$Rm)))]>;
  def v2f64 : BaseSIMDThreeSameVectorTied<1, U, {S,0b11}, {0b11,opc}, V128,
                                      asm, ".2d",
     [(set (v2f64 V128:$dst),
           (OpNode (v2f64 V128:$Rd), (v2f64 V128:$Rn), (v2f64 V128:$Rm)))]>;
}

// As above, but D and B sized elements unsupported.
multiclass SIMDThreeSameVectorHS<bit U, bits<5> opc, string asm,
                                SDPatternOperator OpNode> {
  def v4i16 : BaseSIMDThreeSameVector<0, U, 0b011, opc, V64,
                                      asm, ".4h",
        [(set (v4i16 V64:$Rd), (OpNode (v4i16 V64:$Rn), (v4i16 V64:$Rm)))]>;
  def v8i16 : BaseSIMDThreeSameVector<1, U, 0b011, opc, V128,
                                      asm, ".8h",
        [(set (v8i16 V128:$Rd), (OpNode (v8i16 V128:$Rn), (v8i16 V128:$Rm)))]>;
  def v2i32 : BaseSIMDThreeSameVector<0, U, 0b101, opc, V64,
                                      asm, ".2s",
        [(set (v2i32 V64:$Rd), (OpNode (v2i32 V64:$Rn), (v2i32 V64:$Rm)))]>;
  def v4i32 : BaseSIMDThreeSameVector<1, U, 0b101, opc, V128,
                                      asm, ".4s",
        [(set (v4i32 V128:$Rd), (OpNode (v4i32 V128:$Rn), (v4i32 V128:$Rm)))]>;
}

// Logical three vector ops share opcode bits, and only use B sized elements.
multiclass SIMDLogicalThreeVector<bit U, bits<2> size, string asm,
                                  SDPatternOperator OpNode = null_frag> {
  def v8i8  : BaseSIMDThreeSameVector<0, U, {size,1}, 0b00011, V64,
                                     asm, ".8b",
                         [(set (v8i8 V64:$Rd), (OpNode V64:$Rn, V64:$Rm))]>;
  def v16i8  : BaseSIMDThreeSameVector<1, U, {size,1}, 0b00011, V128,
                                     asm, ".16b",
                         [(set (v16i8 V128:$Rd), (OpNode V128:$Rn, V128:$Rm))]>;

  def : Pat<(v4i16 (OpNode V64:$LHS, V64:$RHS)),
          (!cast<Instruction>(NAME#"v8i8") V64:$LHS, V64:$RHS)>;
  def : Pat<(v2i32 (OpNode V64:$LHS, V64:$RHS)),
          (!cast<Instruction>(NAME#"v8i8") V64:$LHS, V64:$RHS)>;
  def : Pat<(v1i64 (OpNode V64:$LHS, V64:$RHS)),
          (!cast<Instruction>(NAME#"v8i8") V64:$LHS, V64:$RHS)>;

  def : Pat<(v8i16 (OpNode V128:$LHS, V128:$RHS)),
      (!cast<Instruction>(NAME#"v16i8") V128:$LHS, V128:$RHS)>;
  def : Pat<(v4i32 (OpNode V128:$LHS, V128:$RHS)),
      (!cast<Instruction>(NAME#"v16i8") V128:$LHS, V128:$RHS)>;
  def : Pat<(v2i64 (OpNode V128:$LHS, V128:$RHS)),
      (!cast<Instruction>(NAME#"v16i8") V128:$LHS, V128:$RHS)>;
}

multiclass SIMDLogicalThreeVectorTied<bit U, bits<2> size,
                                  string asm, SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDThreeSameVectorTied<0, U, {size,1}, 0b00011, V64,
                                     asm, ".8b",
             [(set (v8i8 V64:$dst),
                   (OpNode (v8i8 V64:$Rd), (v8i8 V64:$Rn), (v8i8 V64:$Rm)))]>;
  def v16i8  : BaseSIMDThreeSameVectorTied<1, U, {size,1}, 0b00011, V128,
                                     asm, ".16b",
             [(set (v16i8 V128:$dst),
                   (OpNode (v16i8 V128:$Rd), (v16i8 V128:$Rn),
                           (v16i8 V128:$Rm)))]>;

  def : Pat<(v4i16 (OpNode (v4i16 V64:$LHS), (v4i16 V64:$MHS),
                           (v4i16 V64:$RHS))),
          (!cast<Instruction>(NAME#"v8i8")
            V64:$LHS, V64:$MHS, V64:$RHS)>;
  def : Pat<(v2i32 (OpNode (v2i32 V64:$LHS), (v2i32 V64:$MHS),
                           (v2i32 V64:$RHS))),
          (!cast<Instruction>(NAME#"v8i8")
            V64:$LHS, V64:$MHS, V64:$RHS)>;
  def : Pat<(v1i64 (OpNode (v1i64 V64:$LHS), (v1i64 V64:$MHS),
                           (v1i64 V64:$RHS))),
          (!cast<Instruction>(NAME#"v8i8")
            V64:$LHS, V64:$MHS, V64:$RHS)>;

  def : Pat<(v8i16 (OpNode (v8i16 V128:$LHS), (v8i16 V128:$MHS),
                           (v8i16 V128:$RHS))),
      (!cast<Instruction>(NAME#"v16i8")
        V128:$LHS, V128:$MHS, V128:$RHS)>;
  def : Pat<(v4i32 (OpNode (v4i32 V128:$LHS), (v4i32 V128:$MHS),
                           (v4i32 V128:$RHS))),
      (!cast<Instruction>(NAME#"v16i8")
        V128:$LHS, V128:$MHS, V128:$RHS)>;
  def : Pat<(v2i64 (OpNode (v2i64 V128:$LHS), (v2i64 V128:$MHS),
                           (v2i64 V128:$RHS))),
      (!cast<Instruction>(NAME#"v16i8")
        V128:$LHS, V128:$MHS, V128:$RHS)>;
}

// ARMv8.2-A Dot Product Instructions (Vector): These instructions extract
// bytes from S-sized elements.
class BaseSIMDThreeSameVectorDot<bit Q, bit U, string asm, string kind1,
                                 string kind2, RegisterOperand RegType,
                                 ValueType AccumType, ValueType InputType,
                                 SDPatternOperator OpNode> :
        BaseSIMDThreeSameVectorTied<Q, U, 0b100, 0b10010, RegType, asm, kind1,
        [(set (AccumType RegType:$dst),
              (OpNode (AccumType RegType:$Rd),
                      (InputType RegType:$Rn),
                      (InputType RegType:$Rm)))]> {
  let AsmString = !strconcat(asm, "{\t$Rd" # kind1 # ", $Rn" # kind2 # ", $Rm" # kind2 # "}");
}

multiclass SIMDThreeSameVectorDot<bit U, string asm, SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDThreeSameVectorDot<0, U, asm, ".2s", ".8b", V64,
                                         v2i32, v8i8, OpNode>;
  def v16i8 : BaseSIMDThreeSameVectorDot<1, U, asm, ".4s", ".16b", V128,
                                         v4i32, v16i8, OpNode>;
}

// ARMv8.2-A Fused Multiply Add-Long Instructions (Vector): These instructions
// select inputs from 4H vectors and accumulate outputs to a 2S vector (or from
// 8H to 4S, when Q=1).
class BaseSIMDThreeSameVectorFML<bit Q, bit U, bit b13, bits<3> size, string asm, string kind1,
                                 string kind2, RegisterOperand RegType,
                                 ValueType AccumType, ValueType InputType,
                                 SDPatternOperator OpNode> :
        BaseSIMDThreeSameVectorTied<Q, U, size, 0b11101, RegType, asm, kind1,
		[(set (AccumType RegType:$dst),
              (OpNode (AccumType RegType:$Rd),
                      (InputType RegType:$Rn),
                      (InputType RegType:$Rm)))]> {
  let AsmString = !strconcat(asm, "{\t$Rd" # kind1 # ", $Rn" # kind2 # ", $Rm" # kind2 # "}");
  let Inst{13} = b13;
}

multiclass SIMDThreeSameVectorFML<bit U, bit b13, bits<3> size, string asm,
                                  SDPatternOperator OpNode> {
  def v4f16 : BaseSIMDThreeSameVectorFML<0, U, b13, size, asm, ".2s", ".2h", V64,
                                         v2f32, v4f16, OpNode>;
  def v8f16 : BaseSIMDThreeSameVectorFML<1, U, b13, size, asm, ".4s", ".4h", V128,
                                         v4f32, v8f16, OpNode>;
}


//----------------------------------------------------------------------------
// AdvSIMD two register vector instructions.
//----------------------------------------------------------------------------

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDTwoSameVector<bit Q, bit U, bits<2> size, bits<5> opcode,
                            bits<2> size2, RegisterOperand regtype, string asm,
                            string dstkind, string srckind, list<dag> pattern>
  : I<(outs regtype:$Rd), (ins regtype:$Rn), asm,
      "{\t$Rd" # dstkind # ", $Rn" # srckind #
      "|" # dstkind # "\t$Rd, $Rn}", "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size;
  let Inst{21} = 0b1;
  let Inst{20-19} = size2;
  let Inst{18-17} = 0b00;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDTwoSameVectorTied<bit Q, bit U, bits<2> size, bits<5> opcode,
                                bits<2> size2, RegisterOperand regtype,
                                string asm, string dstkind, string srckind,
                                list<dag> pattern>
  : I<(outs regtype:$dst), (ins regtype:$Rd, regtype:$Rn), asm,
      "{\t$Rd" # dstkind # ", $Rn" # srckind #
      "|" # dstkind # "\t$Rd, $Rn}", "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size;
  let Inst{21} = 0b1;
  let Inst{20-19} = size2;
  let Inst{18-17} = 0b00;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

// Supports B, H, and S element sizes.
multiclass SIMDTwoVectorBHS<bit U, bits<5> opc, string asm,
                            SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDTwoSameVector<0, U, 0b00, opc, 0b00, V64,
                                      asm, ".8b", ".8b",
                          [(set (v8i8 V64:$Rd), (OpNode (v8i8 V64:$Rn)))]>;
  def v16i8 : BaseSIMDTwoSameVector<1, U, 0b00, opc, 0b00, V128,
                                      asm, ".16b", ".16b",
                          [(set (v16i8 V128:$Rd), (OpNode (v16i8 V128:$Rn)))]>;
  def v4i16 : BaseSIMDTwoSameVector<0, U, 0b01, opc, 0b00, V64,
                                      asm, ".4h", ".4h",
                          [(set (v4i16 V64:$Rd), (OpNode (v4i16 V64:$Rn)))]>;
  def v8i16 : BaseSIMDTwoSameVector<1, U, 0b01, opc, 0b00, V128,
                                      asm, ".8h", ".8h",
                          [(set (v8i16 V128:$Rd), (OpNode (v8i16 V128:$Rn)))]>;
  def v2i32 : BaseSIMDTwoSameVector<0, U, 0b10, opc, 0b00, V64,
                                      asm, ".2s", ".2s",
                          [(set (v2i32 V64:$Rd), (OpNode (v2i32 V64:$Rn)))]>;
  def v4i32 : BaseSIMDTwoSameVector<1, U, 0b10, opc, 0b00, V128,
                                      asm, ".4s", ".4s",
                          [(set (v4i32 V128:$Rd), (OpNode (v4i32 V128:$Rn)))]>;
}

class BaseSIMDVectorLShiftLongBySize<bit Q, bits<2> size,
                            RegisterOperand regtype, string asm, string dstkind,
                            string srckind, string amount>
  : I<(outs V128:$Rd), (ins regtype:$Rn), asm,
      "{\t$Rd" # dstkind # ", $Rn" # srckind # ", #" # amount #
      "|" # dstkind # "\t$Rd, $Rn, #" #  amount # "}", "", []>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29-24} = 0b101110;
  let Inst{23-22} = size;
  let Inst{21-10} = 0b100001001110;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SIMDVectorLShiftLongBySizeBHS {
  let hasSideEffects = 0 in {
  def v8i8  : BaseSIMDVectorLShiftLongBySize<0, 0b00, V64,
                                             "shll", ".8h",  ".8b", "8">;
  def v16i8 : BaseSIMDVectorLShiftLongBySize<1, 0b00, V128,
                                             "shll2", ".8h", ".16b", "8">;
  def v4i16 : BaseSIMDVectorLShiftLongBySize<0, 0b01, V64,
                                             "shll", ".4s",  ".4h", "16">;
  def v8i16 : BaseSIMDVectorLShiftLongBySize<1, 0b01, V128,
                                             "shll2", ".4s", ".8h", "16">;
  def v2i32 : BaseSIMDVectorLShiftLongBySize<0, 0b10, V64,
                                             "shll", ".2d",  ".2s", "32">;
  def v4i32 : BaseSIMDVectorLShiftLongBySize<1, 0b10, V128,
                                             "shll2", ".2d", ".4s", "32">;
  }
}

// Supports all element sizes.
multiclass SIMDLongTwoVector<bit U, bits<5> opc, string asm,
                             SDPatternOperator OpNode> {
  def v8i8_v4i16  : BaseSIMDTwoSameVector<0, U, 0b00, opc, 0b00, V64,
                                      asm, ".4h", ".8b",
               [(set (v4i16 V64:$Rd), (OpNode (v8i8 V64:$Rn)))]>;
  def v16i8_v8i16 : BaseSIMDTwoSameVector<1, U, 0b00, opc, 0b00, V128,
                                      asm, ".8h", ".16b",
               [(set (v8i16 V128:$Rd), (OpNode (v16i8 V128:$Rn)))]>;
  def v4i16_v2i32 : BaseSIMDTwoSameVector<0, U, 0b01, opc, 0b00, V64,
                                      asm, ".2s", ".4h",
               [(set (v2i32 V64:$Rd), (OpNode (v4i16 V64:$Rn)))]>;
  def v8i16_v4i32 : BaseSIMDTwoSameVector<1, U, 0b01, opc, 0b00, V128,
                                      asm, ".4s", ".8h",
               [(set (v4i32 V128:$Rd), (OpNode (v8i16 V128:$Rn)))]>;
  def v2i32_v1i64 : BaseSIMDTwoSameVector<0, U, 0b10, opc, 0b00, V64,
                                      asm, ".1d", ".2s",
               [(set (v1i64 V64:$Rd), (OpNode (v2i32 V64:$Rn)))]>;
  def v4i32_v2i64 : BaseSIMDTwoSameVector<1, U, 0b10, opc, 0b00, V128,
                                      asm, ".2d", ".4s",
               [(set (v2i64 V128:$Rd), (OpNode (v4i32 V128:$Rn)))]>;
}

multiclass SIMDLongTwoVectorTied<bit U, bits<5> opc, string asm,
                                 SDPatternOperator OpNode> {
  def v8i8_v4i16  : BaseSIMDTwoSameVectorTied<0, U, 0b00, opc, 0b00, V64,
                                          asm, ".4h", ".8b",
      [(set (v4i16 V64:$dst), (OpNode (v4i16 V64:$Rd),
                                      (v8i8 V64:$Rn)))]>;
  def v16i8_v8i16 : BaseSIMDTwoSameVectorTied<1, U, 0b00, opc, 0b00, V128,
                                          asm, ".8h", ".16b",
      [(set (v8i16 V128:$dst), (OpNode (v8i16 V128:$Rd),
                                      (v16i8 V128:$Rn)))]>;
  def v4i16_v2i32 : BaseSIMDTwoSameVectorTied<0, U, 0b01, opc, 0b00, V64,
                                          asm, ".2s", ".4h",
      [(set (v2i32 V64:$dst), (OpNode (v2i32 V64:$Rd),
                                      (v4i16 V64:$Rn)))]>;
  def v8i16_v4i32 : BaseSIMDTwoSameVectorTied<1, U, 0b01, opc, 0b00, V128,
                                          asm, ".4s", ".8h",
      [(set (v4i32 V128:$dst), (OpNode (v4i32 V128:$Rd),
                                      (v8i16 V128:$Rn)))]>;
  def v2i32_v1i64 : BaseSIMDTwoSameVectorTied<0, U, 0b10, opc, 0b00, V64,
                                          asm, ".1d", ".2s",
      [(set (v1i64 V64:$dst), (OpNode (v1i64 V64:$Rd),
                                      (v2i32 V64:$Rn)))]>;
  def v4i32_v2i64 : BaseSIMDTwoSameVectorTied<1, U, 0b10, opc, 0b00, V128,
                                          asm, ".2d", ".4s",
      [(set (v2i64 V128:$dst), (OpNode (v2i64 V128:$Rd),
                                      (v4i32 V128:$Rn)))]>;
}

// Supports all element sizes, except 1xD.
multiclass SIMDTwoVectorBHSDTied<bit U, bits<5> opc, string asm,
                                  SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDTwoSameVectorTied<0, U, 0b00, opc, 0b00, V64,
                                    asm, ".8b", ".8b",
    [(set (v8i8 V64:$dst), (OpNode (v8i8 V64:$Rd), (v8i8 V64:$Rn)))]>;
  def v16i8 : BaseSIMDTwoSameVectorTied<1, U, 0b00, opc, 0b00, V128,
                                    asm, ".16b", ".16b",
    [(set (v16i8 V128:$dst), (OpNode (v16i8 V128:$Rd), (v16i8 V128:$Rn)))]>;
  def v4i16 : BaseSIMDTwoSameVectorTied<0, U, 0b01, opc, 0b00, V64,
                                    asm, ".4h", ".4h",
    [(set (v4i16 V64:$dst), (OpNode (v4i16 V64:$Rd), (v4i16 V64:$Rn)))]>;
  def v8i16 : BaseSIMDTwoSameVectorTied<1, U, 0b01, opc, 0b00, V128,
                                    asm, ".8h", ".8h",
    [(set (v8i16 V128:$dst), (OpNode (v8i16 V128:$Rd), (v8i16 V128:$Rn)))]>;
  def v2i32 : BaseSIMDTwoSameVectorTied<0, U, 0b10, opc, 0b00, V64,
                                    asm, ".2s", ".2s",
    [(set (v2i32 V64:$dst), (OpNode (v2i32 V64:$Rd), (v2i32 V64:$Rn)))]>;
  def v4i32 : BaseSIMDTwoSameVectorTied<1, U, 0b10, opc, 0b00, V128,
                                    asm, ".4s", ".4s",
    [(set (v4i32 V128:$dst), (OpNode (v4i32 V128:$Rd), (v4i32 V128:$Rn)))]>;
  def v2i64 : BaseSIMDTwoSameVectorTied<1, U, 0b11, opc, 0b00, V128,
                                    asm, ".2d", ".2d",
    [(set (v2i64 V128:$dst), (OpNode (v2i64 V128:$Rd), (v2i64 V128:$Rn)))]>;
}

multiclass SIMDTwoVectorBHSD<bit U, bits<5> opc, string asm,
                             SDPatternOperator OpNode = null_frag> {
  def v8i8  : BaseSIMDTwoSameVector<0, U, 0b00, opc, 0b00, V64,
                                asm, ".8b", ".8b",
    [(set (v8i8 V64:$Rd), (OpNode (v8i8 V64:$Rn)))]>;
  def v16i8 : BaseSIMDTwoSameVector<1, U, 0b00, opc, 0b00, V128,
                                asm, ".16b", ".16b",
    [(set (v16i8 V128:$Rd), (OpNode (v16i8 V128:$Rn)))]>;
  def v4i16 : BaseSIMDTwoSameVector<0, U, 0b01, opc, 0b00, V64,
                                asm, ".4h", ".4h",
    [(set (v4i16 V64:$Rd), (OpNode (v4i16 V64:$Rn)))]>;
  def v8i16 : BaseSIMDTwoSameVector<1, U, 0b01, opc, 0b00, V128,
                                asm, ".8h", ".8h",
    [(set (v8i16 V128:$Rd), (OpNode (v8i16 V128:$Rn)))]>;
  def v2i32 : BaseSIMDTwoSameVector<0, U, 0b10, opc, 0b00, V64,
                                asm, ".2s", ".2s",
    [(set (v2i32 V64:$Rd), (OpNode (v2i32 V64:$Rn)))]>;
  def v4i32 : BaseSIMDTwoSameVector<1, U, 0b10, opc, 0b00, V128,
                                asm, ".4s", ".4s",
    [(set (v4i32 V128:$Rd), (OpNode (v4i32 V128:$Rn)))]>;
  def v2i64 : BaseSIMDTwoSameVector<1, U, 0b11, opc, 0b00, V128,
                                asm, ".2d", ".2d",
    [(set (v2i64 V128:$Rd), (OpNode (v2i64 V128:$Rn)))]>;
}


// Supports only B element sizes.
multiclass SIMDTwoVectorB<bit U, bits<2> size, bits<5> opc, string asm,
                          SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDTwoSameVector<0, U, size, opc, 0b00, V64,
                                asm, ".8b", ".8b",
                    [(set (v8i8 V64:$Rd), (OpNode (v8i8 V64:$Rn)))]>;
  def v16i8 : BaseSIMDTwoSameVector<1, U, size, opc, 0b00, V128,
                                asm, ".16b", ".16b",
                    [(set (v16i8 V128:$Rd), (OpNode (v16i8 V128:$Rn)))]>;

}

// Supports only B and H element sizes.
multiclass SIMDTwoVectorBH<bit U, bits<5> opc, string asm,
                                SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDTwoSameVector<0, U, 0b00, opc, 0b00, V64,
                                asm, ".8b", ".8b",
                    [(set (v8i8 V64:$Rd), (OpNode V64:$Rn))]>;
  def v16i8 : BaseSIMDTwoSameVector<1, U, 0b00, opc, 0b00, V128,
                                asm, ".16b", ".16b",
                    [(set (v16i8 V128:$Rd), (OpNode V128:$Rn))]>;
  def v4i16 : BaseSIMDTwoSameVector<0, U, 0b01, opc, 0b00, V64,
                                asm, ".4h", ".4h",
                    [(set (v4i16 V64:$Rd), (OpNode V64:$Rn))]>;
  def v8i16 : BaseSIMDTwoSameVector<1, U, 0b01, opc, 0b00, V128,
                                asm, ".8h", ".8h",
                    [(set (v8i16 V128:$Rd), (OpNode V128:$Rn))]>;
}

// Supports H, S and D element sizes, uses high bit of the size field
// as an extra opcode bit.
multiclass SIMDTwoVectorFP<bit U, bit S, bits<5> opc, string asm,
                           SDPatternOperator OpNode> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4f16 : BaseSIMDTwoSameVector<0, U, {S,1}, opc, 0b11, V64,
                                asm, ".4h", ".4h",
                          [(set (v4f16 V64:$Rd), (OpNode (v4f16 V64:$Rn)))]>;
  def v8f16 : BaseSIMDTwoSameVector<1, U, {S,1}, opc, 0b11, V128,
                                asm, ".8h", ".8h",
                          [(set (v8f16 V128:$Rd), (OpNode (v8f16 V128:$Rn)))]>;
  } // Predicates = [HasNEON, HasFullFP16]
  def v2f32 : BaseSIMDTwoSameVector<0, U, {S,0}, opc, 0b00, V64,
                                asm, ".2s", ".2s",
                          [(set (v2f32 V64:$Rd), (OpNode (v2f32 V64:$Rn)))]>;
  def v4f32 : BaseSIMDTwoSameVector<1, U, {S,0}, opc, 0b00, V128,
                                asm, ".4s", ".4s",
                          [(set (v4f32 V128:$Rd), (OpNode (v4f32 V128:$Rn)))]>;
  def v2f64 : BaseSIMDTwoSameVector<1, U, {S,1}, opc, 0b00, V128,
                                asm, ".2d", ".2d",
                          [(set (v2f64 V128:$Rd), (OpNode (v2f64 V128:$Rn)))]>;
}

// Supports only S and D element sizes
multiclass SIMDTwoVectorSD<bit U, bits<5> opc, string asm,
                           SDPatternOperator OpNode = null_frag> {

  def v2f32 : BaseSIMDTwoSameVector<0, U, 00, opc, 0b00, V64,
                                asm, ".2s", ".2s",
                          [(set (v2f32 V64:$Rd), (OpNode (v2f32 V64:$Rn)))]>;
  def v4f32 : BaseSIMDTwoSameVector<1, U, 00, opc, 0b00, V128,
                                asm, ".4s", ".4s",
                          [(set (v4f32 V128:$Rd), (OpNode (v4f32 V128:$Rn)))]>;
  def v2f64 : BaseSIMDTwoSameVector<1, U, 01, opc, 0b00, V128,
                                asm, ".2d", ".2d",
                          [(set (v2f64 V128:$Rd), (OpNode (v2f64 V128:$Rn)))]>;
}

multiclass FRIntNNTVector<bit U, bit op, string asm,
                          SDPatternOperator OpNode = null_frag> :
           SIMDTwoVectorSD<U, {0b1111,op}, asm, OpNode>;

// Supports only S element size.
multiclass SIMDTwoVectorS<bit U, bit S, bits<5> opc, string asm,
                           SDPatternOperator OpNode> {
  def v2i32 : BaseSIMDTwoSameVector<0, U, {S,0}, opc, 0b00, V64,
                                asm, ".2s", ".2s",
                          [(set (v2i32 V64:$Rd), (OpNode (v2i32 V64:$Rn)))]>;
  def v4i32 : BaseSIMDTwoSameVector<1, U, {S,0}, opc, 0b00, V128,
                                asm, ".4s", ".4s",
                          [(set (v4i32 V128:$Rd), (OpNode (v4i32 V128:$Rn)))]>;
}


multiclass SIMDTwoVectorFPToInt<bit U, bit S, bits<5> opc, string asm,
                           SDPatternOperator OpNode> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4f16 : BaseSIMDTwoSameVector<0, U, {S,1}, opc, 0b11, V64,
                                asm, ".4h", ".4h",
                          [(set (v4i16 V64:$Rd), (OpNode (v4f16 V64:$Rn)))]>;
  def v8f16 : BaseSIMDTwoSameVector<1, U, {S,1}, opc, 0b11, V128,
                                asm, ".8h", ".8h",
                          [(set (v8i16 V128:$Rd), (OpNode (v8f16 V128:$Rn)))]>;
  } // Predicates = [HasNEON, HasFullFP16]
  def v2f32 : BaseSIMDTwoSameVector<0, U, {S,0}, opc, 0b00, V64,
                                asm, ".2s", ".2s",
                          [(set (v2i32 V64:$Rd), (OpNode (v2f32 V64:$Rn)))]>;
  def v4f32 : BaseSIMDTwoSameVector<1, U, {S,0}, opc, 0b00, V128,
                                asm, ".4s", ".4s",
                          [(set (v4i32 V128:$Rd), (OpNode (v4f32 V128:$Rn)))]>;
  def v2f64 : BaseSIMDTwoSameVector<1, U, {S,1}, opc, 0b00, V128,
                                asm, ".2d", ".2d",
                          [(set (v2i64 V128:$Rd), (OpNode (v2f64 V128:$Rn)))]>;
}

multiclass SIMDTwoVectorIntToFP<bit U, bit S, bits<5> opc, string asm,
                           SDPatternOperator OpNode> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4f16 : BaseSIMDTwoSameVector<0, U, {S,1}, opc, 0b11, V64,
                                asm, ".4h", ".4h",
                          [(set (v4f16 V64:$Rd), (OpNode (v4i16 V64:$Rn)))]>;
  def v8f16 : BaseSIMDTwoSameVector<1, U, {S,1}, opc, 0b11, V128,
                                asm, ".8h", ".8h",
                          [(set (v8f16 V128:$Rd), (OpNode (v8i16 V128:$Rn)))]>;
  } // Predicates = [HasNEON, HasFullFP16]
  def v2f32 : BaseSIMDTwoSameVector<0, U, {S,0}, opc, 0b00, V64,
                                asm, ".2s", ".2s",
                          [(set (v2f32 V64:$Rd), (OpNode (v2i32 V64:$Rn)))]>;
  def v4f32 : BaseSIMDTwoSameVector<1, U, {S,0}, opc, 0b00, V128,
                                asm, ".4s", ".4s",
                          [(set (v4f32 V128:$Rd), (OpNode (v4i32 V128:$Rn)))]>;
  def v2f64 : BaseSIMDTwoSameVector<1, U, {S,1}, opc, 0b00, V128,
                                asm, ".2d", ".2d",
                          [(set (v2f64 V128:$Rd), (OpNode (v2i64 V128:$Rn)))]>;
}


class BaseSIMDMixedTwoVector<bit Q, bit U, bits<2> size, bits<5> opcode,
                           RegisterOperand inreg, RegisterOperand outreg,
                           string asm, string outkind, string inkind,
                           list<dag> pattern>
  : I<(outs outreg:$Rd), (ins inreg:$Rn), asm,
      "{\t$Rd" # outkind # ", $Rn" # inkind #
      "|" # outkind # "\t$Rd, $Rn}", "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size;
  let Inst{21-17} = 0b10000;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

class BaseSIMDMixedTwoVectorTied<bit Q, bit U, bits<2> size, bits<5> opcode,
                           RegisterOperand inreg, RegisterOperand outreg,
                           string asm, string outkind, string inkind,
                           list<dag> pattern>
  : I<(outs outreg:$dst), (ins outreg:$Rd, inreg:$Rn), asm,
      "{\t$Rd" # outkind # ", $Rn" # inkind #
      "|" # outkind # "\t$Rd, $Rn}", "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size;
  let Inst{21-17} = 0b10000;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SIMDMixedTwoVector<bit U, bits<5> opc, string asm,
                              SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDMixedTwoVector<0, U, 0b00, opc, V128, V64,
                                      asm, ".8b", ".8h",
        [(set (v8i8 V64:$Rd), (OpNode (v8i16 V128:$Rn)))]>;
  def v16i8 : BaseSIMDMixedTwoVectorTied<1, U, 0b00, opc, V128, V128,
                                      asm#"2", ".16b", ".8h", []>;
  def v4i16 : BaseSIMDMixedTwoVector<0, U, 0b01, opc, V128, V64,
                                      asm, ".4h", ".4s",
        [(set (v4i16 V64:$Rd), (OpNode (v4i32 V128:$Rn)))]>;
  def v8i16 : BaseSIMDMixedTwoVectorTied<1, U, 0b01, opc, V128, V128,
                                      asm#"2", ".8h", ".4s", []>;
  def v2i32 : BaseSIMDMixedTwoVector<0, U, 0b10, opc, V128, V64,
                                      asm, ".2s", ".2d",
        [(set (v2i32 V64:$Rd), (OpNode (v2i64 V128:$Rn)))]>;
  def v4i32 : BaseSIMDMixedTwoVectorTied<1, U, 0b10, opc, V128, V128,
                                      asm#"2", ".4s", ".2d", []>;

  def : Pat<(concat_vectors (v8i8 V64:$Rd), (OpNode (v8i16 V128:$Rn))),
            (!cast<Instruction>(NAME # "v16i8")
                (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), V128:$Rn)>;
  def : Pat<(concat_vectors (v4i16 V64:$Rd), (OpNode (v4i32 V128:$Rn))),
            (!cast<Instruction>(NAME # "v8i16")
                (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), V128:$Rn)>;
  def : Pat<(concat_vectors (v2i32 V64:$Rd), (OpNode (v2i64 V128:$Rn))),
            (!cast<Instruction>(NAME # "v4i32")
                (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), V128:$Rn)>;
}

class BaseSIMDCmpTwoVector<bit Q, bit U, bits<2> size, bits<2> size2,
                           bits<5> opcode, RegisterOperand regtype, string asm,
                           string kind, string zero, ValueType dty,
                           ValueType sty, SDNode OpNode>
  : I<(outs regtype:$Rd), (ins regtype:$Rn), asm,
      "{\t$Rd" # kind # ", $Rn" # kind # ", #" # zero #
      "|" # kind # "\t$Rd, $Rn, #" # zero # "}", "",
      [(set (dty regtype:$Rd), (OpNode (sty regtype:$Rn)))]>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size;
  let Inst{21} = 0b1;
  let Inst{20-19} = size2;
  let Inst{18-17} = 0b00;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

// Comparisons support all element sizes, except 1xD.
multiclass SIMDCmpTwoVector<bit U, bits<5> opc, string asm,
                            SDNode OpNode> {
  def v8i8rz  : BaseSIMDCmpTwoVector<0, U, 0b00, 0b00, opc, V64,
                                     asm, ".8b", "0",
                                     v8i8, v8i8, OpNode>;
  def v16i8rz : BaseSIMDCmpTwoVector<1, U, 0b00, 0b00, opc, V128,
                                     asm, ".16b", "0",
                                     v16i8, v16i8, OpNode>;
  def v4i16rz : BaseSIMDCmpTwoVector<0, U, 0b01, 0b00, opc, V64,
                                     asm, ".4h", "0",
                                     v4i16, v4i16, OpNode>;
  def v8i16rz : BaseSIMDCmpTwoVector<1, U, 0b01, 0b00, opc, V128,
                                     asm, ".8h", "0",
                                     v8i16, v8i16, OpNode>;
  def v2i32rz : BaseSIMDCmpTwoVector<0, U, 0b10, 0b00, opc, V64,
                                     asm, ".2s", "0",
                                     v2i32, v2i32, OpNode>;
  def v4i32rz : BaseSIMDCmpTwoVector<1, U, 0b10, 0b00, opc, V128,
                                     asm, ".4s", "0",
                                     v4i32, v4i32, OpNode>;
  def v2i64rz : BaseSIMDCmpTwoVector<1, U, 0b11, 0b00, opc, V128,
                                     asm, ".2d", "0",
                                     v2i64, v2i64, OpNode>;
}

// FP Comparisons support only S and D element sizes (and H for v8.2a).
multiclass SIMDFPCmpTwoVector<bit U, bit S, bits<5> opc,
                              string asm, SDNode OpNode> {

  let Predicates = [HasNEON, HasFullFP16] in {
  def v4i16rz : BaseSIMDCmpTwoVector<0, U, {S,1}, 0b11, opc, V64,
                                     asm, ".4h", "0.0",
                                     v4i16, v4f16, OpNode>;
  def v8i16rz : BaseSIMDCmpTwoVector<1, U, {S,1}, 0b11, opc, V128,
                                     asm, ".8h", "0.0",
                                     v8i16, v8f16, OpNode>;
  } // Predicates = [HasNEON, HasFullFP16]
  def v2i32rz : BaseSIMDCmpTwoVector<0, U, {S,0}, 0b00, opc, V64,
                                     asm, ".2s", "0.0",
                                     v2i32, v2f32, OpNode>;
  def v4i32rz : BaseSIMDCmpTwoVector<1, U, {S,0}, 0b00, opc, V128,
                                     asm, ".4s", "0.0",
                                     v4i32, v4f32, OpNode>;
  def v2i64rz : BaseSIMDCmpTwoVector<1, U, {S,1}, 0b00, opc, V128,
                                     asm, ".2d", "0.0",
                                     v2i64, v2f64, OpNode>;

  let Predicates = [HasNEON, HasFullFP16] in {
  def : InstAlias<asm # "\t$Vd.4h, $Vn.4h, #0",
                  (!cast<Instruction>(NAME # v4i16rz) V64:$Vd, V64:$Vn), 0>;
  def : InstAlias<asm # "\t$Vd.8h, $Vn.8h, #0",
                  (!cast<Instruction>(NAME # v8i16rz) V128:$Vd, V128:$Vn), 0>;
  }
  def : InstAlias<asm # "\t$Vd.2s, $Vn.2s, #0",
                  (!cast<Instruction>(NAME # v2i32rz) V64:$Vd, V64:$Vn), 0>;
  def : InstAlias<asm # "\t$Vd.4s, $Vn.4s, #0",
                  (!cast<Instruction>(NAME # v4i32rz) V128:$Vd, V128:$Vn), 0>;
  def : InstAlias<asm # "\t$Vd.2d, $Vn.2d, #0",
                  (!cast<Instruction>(NAME # v2i64rz) V128:$Vd, V128:$Vn), 0>;
  let Predicates = [HasNEON, HasFullFP16] in {
  def : InstAlias<asm # ".4h\t$Vd, $Vn, #0",
                  (!cast<Instruction>(NAME # v4i16rz) V64:$Vd, V64:$Vn), 0>;
  def : InstAlias<asm # ".8h\t$Vd, $Vn, #0",
                  (!cast<Instruction>(NAME # v8i16rz) V128:$Vd, V128:$Vn), 0>;
  }
  def : InstAlias<asm # ".2s\t$Vd, $Vn, #0",
                  (!cast<Instruction>(NAME # v2i32rz) V64:$Vd, V64:$Vn), 0>;
  def : InstAlias<asm # ".4s\t$Vd, $Vn, #0",
                  (!cast<Instruction>(NAME # v4i32rz) V128:$Vd, V128:$Vn), 0>;
  def : InstAlias<asm # ".2d\t$Vd, $Vn, #0",
                  (!cast<Instruction>(NAME # v2i64rz) V128:$Vd, V128:$Vn), 0>;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDFPCvtTwoVector<bit Q, bit U, bits<2> size, bits<5> opcode,
                             RegisterOperand outtype, RegisterOperand intype,
                             string asm, string VdTy, string VnTy,
                             list<dag> pattern>
  : I<(outs outtype:$Rd), (ins intype:$Rn), asm,
      !strconcat("\t$Rd", VdTy, ", $Rn", VnTy), "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size;
  let Inst{21-17} = 0b10000;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

class BaseSIMDFPCvtTwoVectorTied<bit Q, bit U, bits<2> size, bits<5> opcode,
                             RegisterOperand outtype, RegisterOperand intype,
                             string asm, string VdTy, string VnTy,
                             list<dag> pattern>
  : I<(outs outtype:$dst), (ins outtype:$Rd, intype:$Rn), asm,
      !strconcat("\t$Rd", VdTy, ", $Rn", VnTy), "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size;
  let Inst{21-17} = 0b10000;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SIMDFPWidenTwoVector<bit U, bit S, bits<5> opc, string asm> {
  def v4i16 : BaseSIMDFPCvtTwoVector<0, U, {S,0}, opc, V128, V64,
                                    asm, ".4s", ".4h", []>;
  def v8i16 : BaseSIMDFPCvtTwoVector<1, U, {S,0}, opc, V128, V128,
                                    asm#"2", ".4s", ".8h", []>;
  def v2i32 : BaseSIMDFPCvtTwoVector<0, U, {S,1}, opc, V128, V64,
                                    asm, ".2d", ".2s", []>;
  def v4i32 : BaseSIMDFPCvtTwoVector<1, U, {S,1}, opc, V128, V128,
                                    asm#"2", ".2d", ".4s", []>;
}

multiclass SIMDFPNarrowTwoVector<bit U, bit S, bits<5> opc, string asm> {
  def v4i16 : BaseSIMDFPCvtTwoVector<0, U, {S,0}, opc, V64, V128,
                                    asm, ".4h", ".4s", []>;
  def v8i16 : BaseSIMDFPCvtTwoVectorTied<1, U, {S,0}, opc, V128, V128,
                                    asm#"2", ".8h", ".4s", []>;
  def v2i32 : BaseSIMDFPCvtTwoVector<0, U, {S,1}, opc, V64, V128,
                                    asm, ".2s", ".2d", []>;
  def v4i32 : BaseSIMDFPCvtTwoVectorTied<1, U, {S,1}, opc, V128, V128,
                                    asm#"2", ".4s", ".2d", []>;
}

multiclass SIMDFPInexactCvtTwoVector<bit U, bit S, bits<5> opc, string asm,
                                     Intrinsic OpNode> {
  def v2f32 : BaseSIMDFPCvtTwoVector<0, U, {S,1}, opc, V64, V128,
                                     asm, ".2s", ".2d",
                          [(set (v2f32 V64:$Rd), (OpNode (v2f64 V128:$Rn)))]>;
  def v4f32 : BaseSIMDFPCvtTwoVectorTied<1, U, {S,1}, opc, V128, V128,
                                    asm#"2", ".4s", ".2d", []>;

  def : Pat<(concat_vectors (v2f32 V64:$Rd), (OpNode (v2f64 V128:$Rn))),
            (!cast<Instruction>(NAME # "v4f32")
                (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), V128:$Rn)>;
}

//----------------------------------------------------------------------------
// AdvSIMD three register different-size vector instructions.
//----------------------------------------------------------------------------

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDDifferentThreeVector<bit U, bits<3> size, bits<4> opcode,
                      RegisterOperand outtype, RegisterOperand intype1,
                      RegisterOperand intype2, string asm,
                      string outkind, string inkind1, string inkind2,
                      list<dag> pattern>
  : I<(outs outtype:$Rd), (ins intype1:$Rn, intype2:$Rm), asm,
      "{\t$Rd" # outkind # ", $Rn" # inkind1 # ", $Rm" # inkind2 #
      "|" # outkind # "\t$Rd, $Rn, $Rm}", "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31}    = 0;
  let Inst{30}    = size{0};
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size{2-1};
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15-12} = opcode;
  let Inst{11-10} = 0b00;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDDifferentThreeVectorTied<bit U, bits<3> size, bits<4> opcode,
                      RegisterOperand outtype, RegisterOperand intype1,
                      RegisterOperand intype2, string asm,
                      string outkind, string inkind1, string inkind2,
                      list<dag> pattern>
  : I<(outs outtype:$dst), (ins outtype:$Rd, intype1:$Rn, intype2:$Rm), asm,
      "{\t$Rd" # outkind # ", $Rn" # inkind1 # ", $Rm" # inkind2 #
      "|" # outkind # "\t$Rd, $Rn, $Rm}", "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31}    = 0;
  let Inst{30}    = size{0};
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size{2-1};
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15-12} = opcode;
  let Inst{11-10} = 0b00;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

// FIXME: TableGen doesn't know how to deal with expanded types that also
//        change the element count (in this case, placing the results in
//        the high elements of the result register rather than the low
//        elements). Until that's fixed, we can't code-gen those.
multiclass SIMDNarrowThreeVectorBHS<bit U, bits<4> opc, string asm,
                                    Intrinsic IntOp> {
  def v8i16_v8i8   : BaseSIMDDifferentThreeVector<U, 0b000, opc,
                                                  V64, V128, V128,
                                                  asm, ".8b", ".8h", ".8h",
     [(set (v8i8 V64:$Rd), (IntOp (v8i16 V128:$Rn), (v8i16 V128:$Rm)))]>;
  def v8i16_v16i8  : BaseSIMDDifferentThreeVectorTied<U, 0b001, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".16b", ".8h", ".8h",
     []>;
  def v4i32_v4i16  : BaseSIMDDifferentThreeVector<U, 0b010, opc,
                                                  V64, V128, V128,
                                                  asm, ".4h", ".4s", ".4s",
     [(set (v4i16 V64:$Rd), (IntOp (v4i32 V128:$Rn), (v4i32 V128:$Rm)))]>;
  def v4i32_v8i16  : BaseSIMDDifferentThreeVectorTied<U, 0b011, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".8h", ".4s", ".4s",
     []>;
  def v2i64_v2i32  : BaseSIMDDifferentThreeVector<U, 0b100, opc,
                                                  V64, V128, V128,
                                                  asm, ".2s", ".2d", ".2d",
     [(set (v2i32 V64:$Rd), (IntOp (v2i64 V128:$Rn), (v2i64 V128:$Rm)))]>;
  def v2i64_v4i32  : BaseSIMDDifferentThreeVectorTied<U, 0b101, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".4s", ".2d", ".2d",
     []>;


  // Patterns for the '2' variants involve INSERT_SUBREG, which you can't put in
  // a version attached to an instruction.
  def : Pat<(concat_vectors (v8i8 V64:$Rd), (IntOp (v8i16 V128:$Rn),
                                                   (v8i16 V128:$Rm))),
            (!cast<Instruction>(NAME # "v8i16_v16i8")
                (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                V128:$Rn, V128:$Rm)>;
  def : Pat<(concat_vectors (v4i16 V64:$Rd), (IntOp (v4i32 V128:$Rn),
                                                    (v4i32 V128:$Rm))),
            (!cast<Instruction>(NAME # "v4i32_v8i16")
                (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                V128:$Rn, V128:$Rm)>;
  def : Pat<(concat_vectors (v2i32 V64:$Rd), (IntOp (v2i64 V128:$Rn),
                                                    (v2i64 V128:$Rm))),
            (!cast<Instruction>(NAME # "v2i64_v4i32")
                (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                V128:$Rn, V128:$Rm)>;
}

multiclass SIMDDifferentThreeVectorBD<bit U, bits<4> opc, string asm,
                                      Intrinsic IntOp> {
  def v8i8   : BaseSIMDDifferentThreeVector<U, 0b000, opc,
                                            V128, V64, V64,
                                            asm, ".8h", ".8b", ".8b",
      [(set (v8i16 V128:$Rd), (IntOp (v8i8 V64:$Rn), (v8i8 V64:$Rm)))]>;
  def v16i8  : BaseSIMDDifferentThreeVector<U, 0b001, opc,
                                            V128, V128, V128,
                                            asm#"2", ".8h", ".16b", ".16b", []>;
  let Predicates = [HasAES] in {
    def v1i64  : BaseSIMDDifferentThreeVector<U, 0b110, opc,
                                              V128, V64, V64,
                                              asm, ".1q", ".1d", ".1d", []>;
    def v2i64  : BaseSIMDDifferentThreeVector<U, 0b111, opc,
                                              V128, V128, V128,
                                              asm#"2", ".1q", ".2d", ".2d", []>;
  }

  def : Pat<(v8i16 (IntOp (v8i8 (extract_high_v16i8 V128:$Rn)),
                          (v8i8 (extract_high_v16i8 V128:$Rm)))),
      (!cast<Instruction>(NAME#"v16i8") V128:$Rn, V128:$Rm)>;
}

multiclass SIMDLongThreeVectorHS<bit U, bits<4> opc, string asm,
                                 SDPatternOperator OpNode> {
  def v4i16_v4i32  : BaseSIMDDifferentThreeVector<U, 0b010, opc,
                                                  V128, V64, V64,
                                                  asm, ".4s", ".4h", ".4h",
      [(set (v4i32 V128:$Rd), (OpNode (v4i16 V64:$Rn), (v4i16 V64:$Rm)))]>;
  def v8i16_v4i32  : BaseSIMDDifferentThreeVector<U, 0b011, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".4s", ".8h", ".8h",
      [(set (v4i32 V128:$Rd), (OpNode (extract_high_v8i16 V128:$Rn),
                                      (extract_high_v8i16 V128:$Rm)))]>;
  def v2i32_v2i64  : BaseSIMDDifferentThreeVector<U, 0b100, opc,
                                                  V128, V64, V64,
                                                  asm, ".2d", ".2s", ".2s",
      [(set (v2i64 V128:$Rd), (OpNode (v2i32 V64:$Rn), (v2i32 V64:$Rm)))]>;
  def v4i32_v2i64  : BaseSIMDDifferentThreeVector<U, 0b101, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".2d", ".4s", ".4s",
      [(set (v2i64 V128:$Rd), (OpNode (extract_high_v4i32 V128:$Rn),
                                      (extract_high_v4i32 V128:$Rm)))]>;
}

multiclass SIMDLongThreeVectorBHSabdl<bit U, bits<4> opc, string asm,
                                  SDPatternOperator OpNode = null_frag> {
  def v8i8_v8i16   : BaseSIMDDifferentThreeVector<U, 0b000, opc,
                                                  V128, V64, V64,
                                                  asm, ".8h", ".8b", ".8b",
      [(set (v8i16 V128:$Rd),
            (zext (v8i8 (OpNode (v8i8 V64:$Rn), (v8i8 V64:$Rm)))))]>;
  def v16i8_v8i16  : BaseSIMDDifferentThreeVector<U, 0b001, opc,
                                                 V128, V128, V128,
                                                 asm#"2", ".8h", ".16b", ".16b",
      [(set (v8i16 V128:$Rd),
            (zext (v8i8 (OpNode (extract_high_v16i8 V128:$Rn),
                                (extract_high_v16i8 V128:$Rm)))))]>;
  def v4i16_v4i32  : BaseSIMDDifferentThreeVector<U, 0b010, opc,
                                                  V128, V64, V64,
                                                  asm, ".4s", ".4h", ".4h",
      [(set (v4i32 V128:$Rd),
            (zext (v4i16 (OpNode (v4i16 V64:$Rn), (v4i16 V64:$Rm)))))]>;
  def v8i16_v4i32  : BaseSIMDDifferentThreeVector<U, 0b011, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".4s", ".8h", ".8h",
      [(set (v4i32 V128:$Rd),
            (zext (v4i16 (OpNode (extract_high_v8i16 V128:$Rn),
                                  (extract_high_v8i16 V128:$Rm)))))]>;
  def v2i32_v2i64  : BaseSIMDDifferentThreeVector<U, 0b100, opc,
                                                  V128, V64, V64,
                                                  asm, ".2d", ".2s", ".2s",
      [(set (v2i64 V128:$Rd),
            (zext (v2i32 (OpNode (v2i32 V64:$Rn), (v2i32 V64:$Rm)))))]>;
  def v4i32_v2i64  : BaseSIMDDifferentThreeVector<U, 0b101, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".2d", ".4s", ".4s",
      [(set (v2i64 V128:$Rd),
            (zext (v2i32 (OpNode (extract_high_v4i32 V128:$Rn),
                                 (extract_high_v4i32 V128:$Rm)))))]>;
}

multiclass SIMDLongThreeVectorTiedBHSabal<bit U, bits<4> opc,
                                          string asm,
                                          SDPatternOperator OpNode> {
  def v8i8_v8i16   : BaseSIMDDifferentThreeVectorTied<U, 0b000, opc,
                                                  V128, V64, V64,
                                                  asm, ".8h", ".8b", ".8b",
    [(set (v8i16 V128:$dst),
          (add (v8i16 V128:$Rd),
               (zext (v8i8 (OpNode (v8i8 V64:$Rn), (v8i8 V64:$Rm))))))]>;
  def v16i8_v8i16  : BaseSIMDDifferentThreeVectorTied<U, 0b001, opc,
                                                 V128, V128, V128,
                                                 asm#"2", ".8h", ".16b", ".16b",
    [(set (v8i16 V128:$dst),
          (add (v8i16 V128:$Rd),
               (zext (v8i8 (OpNode (extract_high_v16i8 V128:$Rn),
                                   (extract_high_v16i8 V128:$Rm))))))]>;
  def v4i16_v4i32  : BaseSIMDDifferentThreeVectorTied<U, 0b010, opc,
                                                  V128, V64, V64,
                                                  asm, ".4s", ".4h", ".4h",
    [(set (v4i32 V128:$dst),
          (add (v4i32 V128:$Rd),
               (zext (v4i16 (OpNode (v4i16 V64:$Rn), (v4i16 V64:$Rm))))))]>;
  def v8i16_v4i32  : BaseSIMDDifferentThreeVectorTied<U, 0b011, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".4s", ".8h", ".8h",
    [(set (v4i32 V128:$dst),
          (add (v4i32 V128:$Rd),
               (zext (v4i16 (OpNode (extract_high_v8i16 V128:$Rn),
                                    (extract_high_v8i16 V128:$Rm))))))]>;
  def v2i32_v2i64  : BaseSIMDDifferentThreeVectorTied<U, 0b100, opc,
                                                  V128, V64, V64,
                                                  asm, ".2d", ".2s", ".2s",
    [(set (v2i64 V128:$dst),
          (add (v2i64 V128:$Rd),
               (zext (v2i32 (OpNode (v2i32 V64:$Rn), (v2i32 V64:$Rm))))))]>;
  def v4i32_v2i64  : BaseSIMDDifferentThreeVectorTied<U, 0b101, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".2d", ".4s", ".4s",
    [(set (v2i64 V128:$dst),
          (add (v2i64 V128:$Rd),
               (zext (v2i32 (OpNode (extract_high_v4i32 V128:$Rn),
                                    (extract_high_v4i32 V128:$Rm))))))]>;
}

multiclass SIMDLongThreeVectorBHS<bit U, bits<4> opc, string asm,
                                  SDPatternOperator OpNode = null_frag> {
  def v8i8_v8i16   : BaseSIMDDifferentThreeVector<U, 0b000, opc,
                                                  V128, V64, V64,
                                                  asm, ".8h", ".8b", ".8b",
      [(set (v8i16 V128:$Rd), (OpNode (v8i8 V64:$Rn), (v8i8 V64:$Rm)))]>;
  def v16i8_v8i16  : BaseSIMDDifferentThreeVector<U, 0b001, opc,
                                                 V128, V128, V128,
                                                 asm#"2", ".8h", ".16b", ".16b",
      [(set (v8i16 V128:$Rd), (OpNode (extract_high_v16i8 V128:$Rn),
                                      (extract_high_v16i8 V128:$Rm)))]>;
  def v4i16_v4i32  : BaseSIMDDifferentThreeVector<U, 0b010, opc,
                                                  V128, V64, V64,
                                                  asm, ".4s", ".4h", ".4h",
      [(set (v4i32 V128:$Rd), (OpNode (v4i16 V64:$Rn), (v4i16 V64:$Rm)))]>;
  def v8i16_v4i32  : BaseSIMDDifferentThreeVector<U, 0b011, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".4s", ".8h", ".8h",
      [(set (v4i32 V128:$Rd), (OpNode (extract_high_v8i16 V128:$Rn),
                                      (extract_high_v8i16 V128:$Rm)))]>;
  def v2i32_v2i64  : BaseSIMDDifferentThreeVector<U, 0b100, opc,
                                                  V128, V64, V64,
                                                  asm, ".2d", ".2s", ".2s",
      [(set (v2i64 V128:$Rd), (OpNode (v2i32 V64:$Rn), (v2i32 V64:$Rm)))]>;
  def v4i32_v2i64  : BaseSIMDDifferentThreeVector<U, 0b101, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".2d", ".4s", ".4s",
      [(set (v2i64 V128:$Rd), (OpNode (extract_high_v4i32 V128:$Rn),
                                      (extract_high_v4i32 V128:$Rm)))]>;
}

multiclass SIMDLongThreeVectorTiedBHS<bit U, bits<4> opc,
                                      string asm,
                                      SDPatternOperator OpNode> {
  def v8i8_v8i16   : BaseSIMDDifferentThreeVectorTied<U, 0b000, opc,
                                                  V128, V64, V64,
                                                  asm, ".8h", ".8b", ".8b",
    [(set (v8i16 V128:$dst),
          (OpNode (v8i16 V128:$Rd), (v8i8 V64:$Rn), (v8i8 V64:$Rm)))]>;
  def v16i8_v8i16  : BaseSIMDDifferentThreeVectorTied<U, 0b001, opc,
                                                 V128, V128, V128,
                                                 asm#"2", ".8h", ".16b", ".16b",
    [(set (v8i16 V128:$dst),
          (OpNode (v8i16 V128:$Rd),
                  (extract_high_v16i8 V128:$Rn),
                  (extract_high_v16i8 V128:$Rm)))]>;
  def v4i16_v4i32  : BaseSIMDDifferentThreeVectorTied<U, 0b010, opc,
                                                  V128, V64, V64,
                                                  asm, ".4s", ".4h", ".4h",
    [(set (v4i32 V128:$dst),
          (OpNode (v4i32 V128:$Rd), (v4i16 V64:$Rn), (v4i16 V64:$Rm)))]>;
  def v8i16_v4i32  : BaseSIMDDifferentThreeVectorTied<U, 0b011, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".4s", ".8h", ".8h",
    [(set (v4i32 V128:$dst),
          (OpNode (v4i32 V128:$Rd),
                  (extract_high_v8i16 V128:$Rn),
                  (extract_high_v8i16 V128:$Rm)))]>;
  def v2i32_v2i64  : BaseSIMDDifferentThreeVectorTied<U, 0b100, opc,
                                                  V128, V64, V64,
                                                  asm, ".2d", ".2s", ".2s",
    [(set (v2i64 V128:$dst),
          (OpNode (v2i64 V128:$Rd), (v2i32 V64:$Rn), (v2i32 V64:$Rm)))]>;
  def v4i32_v2i64  : BaseSIMDDifferentThreeVectorTied<U, 0b101, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".2d", ".4s", ".4s",
    [(set (v2i64 V128:$dst),
          (OpNode (v2i64 V128:$Rd),
                  (extract_high_v4i32 V128:$Rn),
                  (extract_high_v4i32 V128:$Rm)))]>;
}

multiclass SIMDLongThreeVectorSQDMLXTiedHS<bit U, bits<4> opc, string asm,
                                           SDPatternOperator Accum> {
  def v4i16_v4i32  : BaseSIMDDifferentThreeVectorTied<U, 0b010, opc,
                                                  V128, V64, V64,
                                                  asm, ".4s", ".4h", ".4h",
    [(set (v4i32 V128:$dst),
          (Accum (v4i32 V128:$Rd),
                 (v4i32 (int_aarch64_neon_sqdmull (v4i16 V64:$Rn),
                                                (v4i16 V64:$Rm)))))]>;
  def v8i16_v4i32  : BaseSIMDDifferentThreeVectorTied<U, 0b011, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".4s", ".8h", ".8h",
    [(set (v4i32 V128:$dst),
          (Accum (v4i32 V128:$Rd),
                 (v4i32 (int_aarch64_neon_sqdmull (extract_high_v8i16 V128:$Rn),
                                            (extract_high_v8i16 V128:$Rm)))))]>;
  def v2i32_v2i64  : BaseSIMDDifferentThreeVectorTied<U, 0b100, opc,
                                                  V128, V64, V64,
                                                  asm, ".2d", ".2s", ".2s",
    [(set (v2i64 V128:$dst),
          (Accum (v2i64 V128:$Rd),
                 (v2i64 (int_aarch64_neon_sqdmull (v2i32 V64:$Rn),
                                                (v2i32 V64:$Rm)))))]>;
  def v4i32_v2i64  : BaseSIMDDifferentThreeVectorTied<U, 0b101, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".2d", ".4s", ".4s",
    [(set (v2i64 V128:$dst),
          (Accum (v2i64 V128:$Rd),
                 (v2i64 (int_aarch64_neon_sqdmull (extract_high_v4i32 V128:$Rn),
                                            (extract_high_v4i32 V128:$Rm)))))]>;
}

multiclass SIMDWideThreeVectorBHS<bit U, bits<4> opc, string asm,
                                  SDPatternOperator OpNode> {
  def v8i8_v8i16   : BaseSIMDDifferentThreeVector<U, 0b000, opc,
                                                  V128, V128, V64,
                                                  asm, ".8h", ".8h", ".8b",
       [(set (v8i16 V128:$Rd), (OpNode (v8i16 V128:$Rn), (v8i8 V64:$Rm)))]>;
  def v16i8_v8i16  : BaseSIMDDifferentThreeVector<U, 0b001, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".8h", ".8h", ".16b",
       [(set (v8i16 V128:$Rd), (OpNode (v8i16 V128:$Rn),
                                       (extract_high_v16i8 V128:$Rm)))]>;
  def v4i16_v4i32  : BaseSIMDDifferentThreeVector<U, 0b010, opc,
                                                  V128, V128, V64,
                                                  asm, ".4s", ".4s", ".4h",
       [(set (v4i32 V128:$Rd), (OpNode (v4i32 V128:$Rn), (v4i16 V64:$Rm)))]>;
  def v8i16_v4i32  : BaseSIMDDifferentThreeVector<U, 0b011, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".4s", ".4s", ".8h",
       [(set (v4i32 V128:$Rd), (OpNode (v4i32 V128:$Rn),
                                       (extract_high_v8i16 V128:$Rm)))]>;
  def v2i32_v2i64  : BaseSIMDDifferentThreeVector<U, 0b100, opc,
                                                  V128, V128, V64,
                                                  asm, ".2d", ".2d", ".2s",
       [(set (v2i64 V128:$Rd), (OpNode (v2i64 V128:$Rn), (v2i32 V64:$Rm)))]>;
  def v4i32_v2i64  : BaseSIMDDifferentThreeVector<U, 0b101, opc,
                                                  V128, V128, V128,
                                                  asm#"2", ".2d", ".2d", ".4s",
       [(set (v2i64 V128:$Rd), (OpNode (v2i64 V128:$Rn),
                                       (extract_high_v4i32 V128:$Rm)))]>;
}

//----------------------------------------------------------------------------
// AdvSIMD bitwise extract from vector
//----------------------------------------------------------------------------

class BaseSIMDBitwiseExtract<bit size, RegisterOperand regtype, ValueType vty,
                             string asm, string kind>
  : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm, i32imm:$imm), asm,
      "{\t$Rd" # kind # ", $Rn" # kind # ", $Rm" # kind # ", $imm" #
      "|" # kind # "\t$Rd, $Rn, $Rm, $imm}", "",
      [(set (vty regtype:$Rd),
            (AArch64ext regtype:$Rn, regtype:$Rm, (i32 imm:$imm)))]>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<4> imm;
  let Inst{31}    = 0;
  let Inst{30}    = size;
  let Inst{29-21} = 0b101110000;
  let Inst{20-16} = Rm;
  let Inst{15}    = 0;
  let Inst{14-11} = imm;
  let Inst{10}    = 0;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}


multiclass SIMDBitwiseExtract<string asm> {
  def v8i8  : BaseSIMDBitwiseExtract<0, V64, v8i8, asm, ".8b"> {
    let imm{3} = 0;
  }
  def v16i8 : BaseSIMDBitwiseExtract<1, V128, v16i8, asm, ".16b">;
}

//----------------------------------------------------------------------------
// AdvSIMD zip vector
//----------------------------------------------------------------------------

class BaseSIMDZipVector<bits<3> size, bits<3> opc, RegisterOperand regtype,
                        string asm, string kind, SDNode OpNode, ValueType valty>
  : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm), asm,
      "{\t$Rd" # kind # ", $Rn" # kind # ", $Rm" # kind #
      "|" # kind # "\t$Rd, $Rn, $Rm}", "",
      [(set (valty regtype:$Rd), (OpNode regtype:$Rn, regtype:$Rm))]>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31}    = 0;
  let Inst{30}    = size{0};
  let Inst{29-24} = 0b001110;
  let Inst{23-22} = size{2-1};
  let Inst{21}    = 0;
  let Inst{20-16} = Rm;
  let Inst{15}    = 0;
  let Inst{14-12} = opc;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SIMDZipVector<bits<3>opc, string asm,
                         SDNode OpNode> {
  def v8i8   : BaseSIMDZipVector<0b000, opc, V64,
      asm, ".8b", OpNode, v8i8>;
  def v16i8  : BaseSIMDZipVector<0b001, opc, V128,
      asm, ".16b", OpNode, v16i8>;
  def v4i16  : BaseSIMDZipVector<0b010, opc, V64,
      asm, ".4h", OpNode, v4i16>;
  def v8i16  : BaseSIMDZipVector<0b011, opc, V128,
      asm, ".8h", OpNode, v8i16>;
  def v2i32  : BaseSIMDZipVector<0b100, opc, V64,
      asm, ".2s", OpNode, v2i32>;
  def v4i32  : BaseSIMDZipVector<0b101, opc, V128,
      asm, ".4s", OpNode, v4i32>;
  def v2i64  : BaseSIMDZipVector<0b111, opc, V128,
      asm, ".2d", OpNode, v2i64>;

  def : Pat<(v4f16 (OpNode V64:$Rn, V64:$Rm)),
        (!cast<Instruction>(NAME#"v4i16") V64:$Rn, V64:$Rm)>;
  def : Pat<(v8f16 (OpNode V128:$Rn, V128:$Rm)),
        (!cast<Instruction>(NAME#"v8i16") V128:$Rn, V128:$Rm)>;
  def : Pat<(v2f32 (OpNode V64:$Rn, V64:$Rm)),
        (!cast<Instruction>(NAME#"v2i32") V64:$Rn, V64:$Rm)>;
  def : Pat<(v4f32 (OpNode V128:$Rn, V128:$Rm)),
        (!cast<Instruction>(NAME#"v4i32") V128:$Rn, V128:$Rm)>;
  def : Pat<(v2f64 (OpNode V128:$Rn, V128:$Rm)),
        (!cast<Instruction>(NAME#"v2i64") V128:$Rn, V128:$Rm)>;
}

//----------------------------------------------------------------------------
// AdvSIMD three register scalar instructions
//----------------------------------------------------------------------------

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
class BaseSIMDThreeScalar<bit U, bits<3> size, bits<5> opcode,
                        RegisterClass regtype, string asm,
                        list<dag> pattern>
  : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm), asm,
      "\t$Rd, $Rn, $Rm", "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31-30} = 0b01;
  let Inst{29}    = U;
  let Inst{28-24} = 0b11110;
  let Inst{23-21} = size;
  let Inst{20-16} = Rm;
  let Inst{15-11} = opcode;
  let Inst{10}    = 1;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
class BaseSIMDThreeScalarTied<bit U, bits<2> size, bit R, bits<5> opcode,
                        dag oops, dag iops, string asm,
            list<dag> pattern>
  : I<oops, iops, asm, "\t$Rd, $Rn, $Rm", "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31-30} = 0b01;
  let Inst{29}    = U;
  let Inst{28-24} = 0b11110;
  let Inst{23-22} = size;
  let Inst{21}    = R;
  let Inst{20-16} = Rm;
  let Inst{15-11} = opcode;
  let Inst{10}    = 1;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SIMDThreeScalarD<bit U, bits<5> opc, string asm,
                            SDPatternOperator OpNode> {
  def v1i64  : BaseSIMDThreeScalar<U, 0b111, opc, FPR64, asm,
    [(set (v1i64 FPR64:$Rd), (OpNode (v1i64 FPR64:$Rn), (v1i64 FPR64:$Rm)))]>;
}

multiclass SIMDThreeScalarBHSD<bit U, bits<5> opc, string asm,
                               SDPatternOperator OpNode> {
  def v1i64  : BaseSIMDThreeScalar<U, 0b111, opc, FPR64, asm,
    [(set (v1i64 FPR64:$Rd), (OpNode (v1i64 FPR64:$Rn), (v1i64 FPR64:$Rm)))]>;
  def v1i32  : BaseSIMDThreeScalar<U, 0b101, opc, FPR32, asm, []>;
  def v1i16  : BaseSIMDThreeScalar<U, 0b011, opc, FPR16, asm, []>;
  def v1i8   : BaseSIMDThreeScalar<U, 0b001, opc, FPR8 , asm, []>;

  def : Pat<(i64 (OpNode (i64 FPR64:$Rn), (i64 FPR64:$Rm))),
            (!cast<Instruction>(NAME#"v1i64") FPR64:$Rn, FPR64:$Rm)>;
  def : Pat<(i32 (OpNode (i32 FPR32:$Rn), (i32 FPR32:$Rm))),
            (!cast<Instruction>(NAME#"v1i32") FPR32:$Rn, FPR32:$Rm)>;
}

multiclass SIMDThreeScalarHS<bit U, bits<5> opc, string asm,
                             SDPatternOperator OpNode> {
  def v1i32  : BaseSIMDThreeScalar<U, 0b101, opc, FPR32, asm,
                             [(set FPR32:$Rd, (OpNode FPR32:$Rn, FPR32:$Rm))]>;
  def v1i16  : BaseSIMDThreeScalar<U, 0b011, opc, FPR16, asm, []>;
}

multiclass SIMDThreeScalarHSTied<bit U, bit R, bits<5> opc, string asm,
                                 SDPatternOperator OpNode = null_frag> {
  def v1i32: BaseSIMDThreeScalarTied<U, 0b10, R, opc, (outs FPR32:$dst),
                                     (ins FPR32:$Rd, FPR32:$Rn, FPR32:$Rm),
                                     asm, []>;
  def v1i16: BaseSIMDThreeScalarTied<U, 0b01, R, opc, (outs FPR16:$dst),
                                     (ins FPR16:$Rd, FPR16:$Rn, FPR16:$Rm),
                                     asm, []>;
}

multiclass SIMDFPThreeScalar<bit U, bit S, bits<3> opc, string asm,
                             SDPatternOperator OpNode = null_frag> {
  let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {
    def #NAME#64 : BaseSIMDThreeScalar<U, {S,0b11}, {0b11,opc}, FPR64, asm,
      [(set (f64 FPR64:$Rd), (OpNode (f64 FPR64:$Rn), (f64 FPR64:$Rm)))]>;
    def #NAME#32 : BaseSIMDThreeScalar<U, {S,0b01}, {0b11,opc}, FPR32, asm,
      [(set FPR32:$Rd, (OpNode FPR32:$Rn, FPR32:$Rm))]>;
    let Predicates = [HasNEON, HasFullFP16] in {
    def #NAME#16 : BaseSIMDThreeScalar<U, {S,0b10}, {0b00,opc}, FPR16, asm,
      [(set FPR16:$Rd, (OpNode FPR16:$Rn, FPR16:$Rm))]>;
    } // Predicates = [HasNEON, HasFullFP16]
  }

  def : Pat<(v1f64 (OpNode (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
            (!cast<Instruction>(NAME # "64") FPR64:$Rn, FPR64:$Rm)>;
}

multiclass SIMDThreeScalarFPCmp<bit U, bit S, bits<3> opc, string asm,
                                SDPatternOperator OpNode = null_frag> {
  let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {
    def #NAME#64 : BaseSIMDThreeScalar<U, {S,0b11}, {0b11,opc}, FPR64, asm,
      [(set (i64 FPR64:$Rd), (OpNode (f64 FPR64:$Rn), (f64 FPR64:$Rm)))]>;
    def #NAME#32 : BaseSIMDThreeScalar<U, {S,0b01}, {0b11,opc}, FPR32, asm,
      [(set (i32 FPR32:$Rd), (OpNode (f32 FPR32:$Rn), (f32 FPR32:$Rm)))]>;
    let Predicates = [HasNEON, HasFullFP16] in {
    def #NAME#16 : BaseSIMDThreeScalar<U, {S,0b10}, {0b00,opc}, FPR16, asm,
      []>;
    } // Predicates = [HasNEON, HasFullFP16]
  }

  def : Pat<(v1i64 (OpNode (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
            (!cast<Instruction>(NAME # "64") FPR64:$Rn, FPR64:$Rm)>;
}

class BaseSIMDThreeScalarMixed<bit U, bits<2> size, bits<5> opcode,
              dag oops, dag iops, string asm, string cstr, list<dag> pat>
  : I<oops, iops, asm,
      "\t$Rd, $Rn, $Rm", cstr, pat>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31-30} = 0b01;
  let Inst{29}    = U;
  let Inst{28-24} = 0b11110;
  let Inst{23-22} = size;
  let Inst{21}    = 1;
  let Inst{20-16} = Rm;
  let Inst{15-11} = opcode;
  let Inst{10}    = 0;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
multiclass SIMDThreeScalarMixedHS<bit U, bits<5> opc, string asm,
                                  SDPatternOperator OpNode = null_frag> {
  def i16  : BaseSIMDThreeScalarMixed<U, 0b01, opc,
                                      (outs FPR32:$Rd),
                                      (ins FPR16:$Rn, FPR16:$Rm), asm, "", []>;
  def i32  : BaseSIMDThreeScalarMixed<U, 0b10, opc,
                                      (outs FPR64:$Rd),
                                      (ins FPR32:$Rn, FPR32:$Rm), asm, "",
            [(set (i64 FPR64:$Rd), (OpNode (i32 FPR32:$Rn), (i32 FPR32:$Rm)))]>;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
multiclass SIMDThreeScalarMixedTiedHS<bit U, bits<5> opc, string asm,
                                  SDPatternOperator OpNode = null_frag> {
  def i16  : BaseSIMDThreeScalarMixed<U, 0b01, opc,
                                      (outs FPR32:$dst),
                                      (ins FPR32:$Rd, FPR16:$Rn, FPR16:$Rm),
                                      asm, "$Rd = $dst", []>;
  def i32  : BaseSIMDThreeScalarMixed<U, 0b10, opc,
                                      (outs FPR64:$dst),
                                      (ins FPR64:$Rd, FPR32:$Rn, FPR32:$Rm),
                                      asm, "$Rd = $dst",
            [(set (i64 FPR64:$dst),
                  (OpNode (i64 FPR64:$Rd), (i32 FPR32:$Rn), (i32 FPR32:$Rm)))]>;
}

//----------------------------------------------------------------------------
// AdvSIMD two register scalar instructions
//----------------------------------------------------------------------------

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDTwoScalar<bit U, bits<2> size, bits<2> size2, bits<5> opcode,
                        RegisterClass regtype, RegisterClass regtype2,
                        string asm, list<dag> pat>
  : I<(outs regtype:$Rd), (ins regtype2:$Rn), asm,
      "\t$Rd, $Rn", "", pat>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31-30} = 0b01;
  let Inst{29}    = U;
  let Inst{28-24} = 0b11110;
  let Inst{23-22} = size;
  let Inst{21} = 0b1;
  let Inst{20-19} = size2;
  let Inst{18-17} = 0b00;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDTwoScalarTied<bit U, bits<2> size, bits<5> opcode,
                        RegisterClass regtype, RegisterClass regtype2,
                        string asm, list<dag> pat>
  : I<(outs regtype:$dst), (ins regtype:$Rd, regtype2:$Rn), asm,
      "\t$Rd, $Rn", "$Rd = $dst", pat>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31-30} = 0b01;
  let Inst{29}    = U;
  let Inst{28-24} = 0b11110;
  let Inst{23-22} = size;
  let Inst{21-17} = 0b10000;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}


let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDCmpTwoScalar<bit U, bits<2> size, bits<2> size2, bits<5> opcode,
                        RegisterClass regtype, string asm, string zero>
  : I<(outs regtype:$Rd), (ins regtype:$Rn), asm,
      "\t$Rd, $Rn, #" # zero, "", []>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31-30} = 0b01;
  let Inst{29}    = U;
  let Inst{28-24} = 0b11110;
  let Inst{23-22} = size;
  let Inst{21} = 0b1;
  let Inst{20-19} = size2;
  let Inst{18-17} = 0b00;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

class SIMDInexactCvtTwoScalar<bits<5> opcode, string asm>
  : I<(outs FPR32:$Rd), (ins FPR64:$Rn), asm, "\t$Rd, $Rn", "",
     [(set (f32 FPR32:$Rd), (int_aarch64_sisd_fcvtxn (f64 FPR64:$Rn)))]>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31-17} = 0b011111100110000;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SIMDCmpTwoScalarD<bit U, bits<5> opc, string asm,
                             SDPatternOperator OpNode> {
  def v1i64rz  : BaseSIMDCmpTwoScalar<U, 0b11, 0b00, opc, FPR64, asm, "0">;

  def : Pat<(v1i64 (OpNode FPR64:$Rn)),
            (!cast<Instruction>(NAME # v1i64rz) FPR64:$Rn)>;
}

multiclass SIMDFPCmpTwoScalar<bit U, bit S, bits<5> opc, string asm,
                              SDPatternOperator OpNode> {
  def v1i64rz  : BaseSIMDCmpTwoScalar<U, {S,1}, 0b00, opc, FPR64, asm, "0.0">;
  def v1i32rz  : BaseSIMDCmpTwoScalar<U, {S,0}, 0b00, opc, FPR32, asm, "0.0">;
  let Predicates = [HasNEON, HasFullFP16] in {
  def v1i16rz  : BaseSIMDCmpTwoScalar<U, {S,1}, 0b11, opc, FPR16, asm, "0.0">;
  }

  def : InstAlias<asm # "\t$Rd, $Rn, #0",
                  (!cast<Instruction>(NAME # v1i64rz) FPR64:$Rd, FPR64:$Rn), 0>;
  def : InstAlias<asm # "\t$Rd, $Rn, #0",
                  (!cast<Instruction>(NAME # v1i32rz) FPR32:$Rd, FPR32:$Rn), 0>;
  let Predicates = [HasNEON, HasFullFP16] in {
  def : InstAlias<asm # "\t$Rd, $Rn, #0",
                  (!cast<Instruction>(NAME # v1i16rz) FPR16:$Rd, FPR16:$Rn), 0>;
  }

  def : Pat<(v1i64 (OpNode (v1f64 FPR64:$Rn))),
            (!cast<Instruction>(NAME # v1i64rz) FPR64:$Rn)>;
}

multiclass SIMDTwoScalarD<bit U, bits<5> opc, string asm,
                          SDPatternOperator OpNode = null_frag> {
  def v1i64       : BaseSIMDTwoScalar<U, 0b11, 0b00, opc, FPR64, FPR64, asm,
    [(set (v1i64 FPR64:$Rd), (OpNode (v1i64 FPR64:$Rn)))]>;

  def : Pat<(i64 (OpNode (i64 FPR64:$Rn))),
            (!cast<Instruction>(NAME # "v1i64") FPR64:$Rn)>;
}

multiclass SIMDFPTwoScalar<bit U, bit S, bits<5> opc, string asm> {
  def v1i64       : BaseSIMDTwoScalar<U, {S,1}, 0b00, opc, FPR64, FPR64, asm,[]>;
  def v1i32       : BaseSIMDTwoScalar<U, {S,0}, 0b00, opc, FPR32, FPR32, asm,[]>;
  let Predicates = [HasNEON, HasFullFP16] in {
  def v1f16       : BaseSIMDTwoScalar<U, {S,1}, 0b11, opc, FPR16, FPR16, asm,[]>;
  }
}

multiclass SIMDFPTwoScalarCVT<bit U, bit S, bits<5> opc, string asm,
                              SDPatternOperator OpNode> {
  def v1i64 : BaseSIMDTwoScalar<U, {S,1}, 0b00, opc, FPR64, FPR64, asm,
                                [(set FPR64:$Rd, (OpNode (f64 FPR64:$Rn)))]>;
  def v1i32 : BaseSIMDTwoScalar<U, {S,0}, 0b00, opc, FPR32, FPR32, asm,
                                [(set FPR32:$Rd, (OpNode (f32 FPR32:$Rn)))]>;
  let Predicates = [HasNEON, HasFullFP16] in {
  def v1i16 : BaseSIMDTwoScalar<U, {S,1}, 0b11, opc, FPR16, FPR16, asm,
                                [(set FPR16:$Rd, (OpNode (f16 FPR16:$Rn)))]>;
  }
}

multiclass SIMDTwoScalarBHSD<bit U, bits<5> opc, string asm,
                             SDPatternOperator OpNode = null_frag> {
  let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {
    def v1i64  : BaseSIMDTwoScalar<U, 0b11, 0b00, opc, FPR64, FPR64, asm,
           [(set (i64 FPR64:$Rd), (OpNode (i64 FPR64:$Rn)))]>;
    def v1i32  : BaseSIMDTwoScalar<U, 0b10, 0b00, opc, FPR32, FPR32, asm,
           [(set (i32 FPR32:$Rd), (OpNode (i32 FPR32:$Rn)))]>;
    def v1i16  : BaseSIMDTwoScalar<U, 0b01, 0b00, opc, FPR16, FPR16, asm, []>;
    def v1i8   : BaseSIMDTwoScalar<U, 0b00, 0b00, opc, FPR8 , FPR8 , asm, []>;
  }

  def : Pat<(v1i64 (OpNode (v1i64 FPR64:$Rn))),
            (!cast<Instruction>(NAME # v1i64) FPR64:$Rn)>;
}

multiclass SIMDTwoScalarBHSDTied<bit U, bits<5> opc, string asm,
                                 Intrinsic OpNode> {
  let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {
    def v1i64  : BaseSIMDTwoScalarTied<U, 0b11, opc, FPR64, FPR64, asm,
        [(set (i64 FPR64:$dst), (OpNode (i64 FPR64:$Rd), (i64 FPR64:$Rn)))]>;
    def v1i32  : BaseSIMDTwoScalarTied<U, 0b10, opc, FPR32, FPR32, asm,
        [(set (i32 FPR32:$dst), (OpNode (i32 FPR32:$Rd), (i32 FPR32:$Rn)))]>;
    def v1i16  : BaseSIMDTwoScalarTied<U, 0b01, opc, FPR16, FPR16, asm, []>;
    def v1i8   : BaseSIMDTwoScalarTied<U, 0b00, opc, FPR8 , FPR8 , asm, []>;
  }

  def : Pat<(v1i64 (OpNode (v1i64 FPR64:$Rd), (v1i64 FPR64:$Rn))),
            (!cast<Instruction>(NAME # v1i64) FPR64:$Rd, FPR64:$Rn)>;
}



let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
multiclass SIMDTwoScalarMixedBHS<bit U, bits<5> opc, string asm,
                                 SDPatternOperator OpNode = null_frag> {
  def v1i32  : BaseSIMDTwoScalar<U, 0b10, 0b00, opc, FPR32, FPR64, asm,
        [(set (i32 FPR32:$Rd), (OpNode (i64 FPR64:$Rn)))]>;
  def v1i16  : BaseSIMDTwoScalar<U, 0b01, 0b00, opc, FPR16, FPR32, asm, []>;
  def v1i8   : BaseSIMDTwoScalar<U, 0b00, 0b00, opc, FPR8 , FPR16, asm, []>;
}

//----------------------------------------------------------------------------
// AdvSIMD scalar pairwise instructions
//----------------------------------------------------------------------------

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDPairwiseScalar<bit U, bits<2> size, bits<5> opcode,
                        RegisterOperand regtype, RegisterOperand vectype,
                        string asm, string kind>
  : I<(outs regtype:$Rd), (ins vectype:$Rn), asm,
      "{\t$Rd, $Rn" # kind # "|" # kind # "\t$Rd, $Rn}", "", []>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31-30} = 0b01;
  let Inst{29}    = U;
  let Inst{28-24} = 0b11110;
  let Inst{23-22} = size;
  let Inst{21-17} = 0b11000;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SIMDPairwiseScalarD<bit U, bits<5> opc, string asm> {
  def v2i64p : BaseSIMDPairwiseScalar<U, 0b11, opc, FPR64Op, V128,
                                      asm, ".2d">;
}

multiclass SIMDFPPairwiseScalar<bit S, bits<5> opc, string asm> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v2i16p : BaseSIMDPairwiseScalar<0, {S,0}, opc, FPR16Op, V64,
                                      asm, ".2h">;
  }
  def v2i32p : BaseSIMDPairwiseScalar<1, {S,0}, opc, FPR32Op, V64,
                                      asm, ".2s">;
  def v2i64p : BaseSIMDPairwiseScalar<1, {S,1}, opc, FPR64Op, V128,
                                      asm, ".2d">;
}

//----------------------------------------------------------------------------
// AdvSIMD across lanes instructions
//----------------------------------------------------------------------------

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDAcrossLanes<bit Q, bit U, bits<2> size, bits<5> opcode,
                          RegisterClass regtype, RegisterOperand vectype,
                          string asm, string kind, list<dag> pattern>
  : I<(outs regtype:$Rd), (ins vectype:$Rn), asm,
      "{\t$Rd, $Rn" # kind # "|" # kind # "\t$Rd, $Rn}", "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size;
  let Inst{21-17} = 0b11000;
  let Inst{16-12} = opcode;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SIMDAcrossLanesBHS<bit U, bits<5> opcode,
                              string asm> {
  def v8i8v  : BaseSIMDAcrossLanes<0, U, 0b00, opcode, FPR8,  V64,
                                   asm, ".8b", []>;
  def v16i8v : BaseSIMDAcrossLanes<1, U, 0b00, opcode, FPR8,  V128,
                                   asm, ".16b", []>;
  def v4i16v : BaseSIMDAcrossLanes<0, U, 0b01, opcode, FPR16, V64,
                                   asm, ".4h", []>;
  def v8i16v : BaseSIMDAcrossLanes<1, U, 0b01, opcode, FPR16, V128,
                                   asm, ".8h", []>;
  def v4i32v : BaseSIMDAcrossLanes<1, U, 0b10, opcode, FPR32, V128,
                                   asm, ".4s", []>;
}

multiclass SIMDAcrossLanesHSD<bit U, bits<5> opcode, string asm> {
  def v8i8v  : BaseSIMDAcrossLanes<0, U, 0b00, opcode, FPR16, V64,
                                   asm, ".8b", []>;
  def v16i8v : BaseSIMDAcrossLanes<1, U, 0b00, opcode, FPR16, V128,
                                   asm, ".16b", []>;
  def v4i16v : BaseSIMDAcrossLanes<0, U, 0b01, opcode, FPR32, V64,
                                   asm, ".4h", []>;
  def v8i16v : BaseSIMDAcrossLanes<1, U, 0b01, opcode, FPR32, V128,
                                   asm, ".8h", []>;
  def v4i32v : BaseSIMDAcrossLanes<1, U, 0b10, opcode, FPR64, V128,
                                   asm, ".4s", []>;
}

multiclass SIMDFPAcrossLanes<bits<5> opcode, bit sz1, string asm,
                            Intrinsic intOp> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4i16v : BaseSIMDAcrossLanes<0, 0, {sz1, 0}, opcode, FPR16, V64,
                                   asm, ".4h",
        [(set FPR16:$Rd, (intOp (v4f16 V64:$Rn)))]>;
  def v8i16v : BaseSIMDAcrossLanes<1, 0, {sz1, 0}, opcode, FPR16, V128,
                                   asm, ".8h",
        [(set FPR16:$Rd, (intOp (v8f16 V128:$Rn)))]>;
  } // Predicates = [HasNEON, HasFullFP16]
  def v4i32v : BaseSIMDAcrossLanes<1, 1, {sz1, 0}, opcode, FPR32, V128,
                                   asm, ".4s",
        [(set FPR32:$Rd, (intOp (v4f32 V128:$Rn)))]>;
}

//----------------------------------------------------------------------------
// AdvSIMD INS/DUP instructions
//----------------------------------------------------------------------------

// FIXME: There has got to be a better way to factor these. ugh.

class BaseSIMDInsDup<bit Q, bit op, dag outs, dag ins, string asm,
                     string operands, string constraints, list<dag> pattern>
  : I<outs, ins, asm, operands, constraints, pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31} = 0;
  let Inst{30} = Q;
  let Inst{29} = op;
  let Inst{28-21} = 0b01110000;
  let Inst{15} = 0;
  let Inst{10} = 1;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rd;
}

class SIMDDupFromMain<bit Q, bits<5> imm5, string size, ValueType vectype,
                      RegisterOperand vecreg, RegisterClass regtype>
  : BaseSIMDInsDup<Q, 0, (outs vecreg:$Rd), (ins regtype:$Rn), "dup",
                   "{\t$Rd" # size # ", $Rn" #
                   "|" # size # "\t$Rd, $Rn}", "",
                   [(set (vectype vecreg:$Rd), (AArch64dup regtype:$Rn))]> {
  let Inst{20-16} = imm5;
  let Inst{14-11} = 0b0001;
}

class SIMDDupFromElement<bit Q, string dstkind, string srckind,
                         ValueType vectype, ValueType insreg,
                         RegisterOperand vecreg, Operand idxtype,
                         ValueType elttype, SDNode OpNode>
  : BaseSIMDInsDup<Q, 0, (outs vecreg:$Rd), (ins V128:$Rn, idxtype:$idx), "dup",
                   "{\t$Rd" # dstkind # ", $Rn" # srckind # "$idx" #
                   "|" # dstkind # "\t$Rd, $Rn$idx}", "",
                 [(set (vectype vecreg:$Rd),
                       (OpNode (insreg V128:$Rn), idxtype:$idx))]> {
  let Inst{14-11} = 0b0000;
}

class SIMDDup64FromElement
  : SIMDDupFromElement<1, ".2d", ".d", v2i64, v2i64, V128,
                       VectorIndexD, i64, AArch64duplane64> {
  bits<1> idx;
  let Inst{20} = idx;
  let Inst{19-16} = 0b1000;
}

class SIMDDup32FromElement<bit Q, string size, ValueType vectype,
                           RegisterOperand vecreg>
  : SIMDDupFromElement<Q, size, ".s", vectype, v4i32, vecreg,
                       VectorIndexS, i64, AArch64duplane32> {
  bits<2> idx;
  let Inst{20-19} = idx;
  let Inst{18-16} = 0b100;
}

class SIMDDup16FromElement<bit Q, string size, ValueType vectype,
                           RegisterOperand vecreg>
  : SIMDDupFromElement<Q, size, ".h", vectype, v8i16, vecreg,
                       VectorIndexH, i64, AArch64duplane16> {
  bits<3> idx;
  let Inst{20-18} = idx;
  let Inst{17-16} = 0b10;
}

class SIMDDup8FromElement<bit Q, string size, ValueType vectype,
                          RegisterOperand vecreg>
  : SIMDDupFromElement<Q, size, ".b", vectype, v16i8, vecreg,
                       VectorIndexB, i64, AArch64duplane8> {
  bits<4> idx;
  let Inst{20-17} = idx;
  let Inst{16} = 1;
}

class BaseSIMDMov<bit Q, string size, bits<4> imm4, RegisterClass regtype,
                  Operand idxtype, string asm, list<dag> pattern>
  : BaseSIMDInsDup<Q, 0, (outs regtype:$Rd), (ins V128:$Rn, idxtype:$idx), asm,
                   "{\t$Rd, $Rn" # size # "$idx" #
                   "|" # size # "\t$Rd, $Rn$idx}", "", pattern> {
  let Inst{14-11} = imm4;
}

class SIMDSMov<bit Q, string size, RegisterClass regtype,
               Operand idxtype>
  : BaseSIMDMov<Q, size, 0b0101, regtype, idxtype, "smov", []>;
class SIMDUMov<bit Q, string size, ValueType vectype, RegisterClass regtype,
               Operand idxtype>
  : BaseSIMDMov<Q, size, 0b0111, regtype, idxtype, "umov",
      [(set regtype:$Rd, (vector_extract (vectype V128:$Rn), idxtype:$idx))]>;

class SIMDMovAlias<string asm, string size, Instruction inst,
                   RegisterClass regtype, Operand idxtype>
    : InstAlias<asm#"{\t$dst, $src"#size#"$idx" #
                    "|" # size # "\t$dst, $src$idx}",
                (inst regtype:$dst, V128:$src, idxtype:$idx)>;

multiclass SMov {
  def vi8to32 : SIMDSMov<0, ".b", GPR32, VectorIndexB> {
    bits<4> idx;
    let Inst{20-17} = idx;
    let Inst{16} = 1;
  }
  def vi8to64 : SIMDSMov<1, ".b", GPR64, VectorIndexB> {
    bits<4> idx;
    let Inst{20-17} = idx;
    let Inst{16} = 1;
  }
  def vi16to32 : SIMDSMov<0, ".h", GPR32, VectorIndexH> {
    bits<3> idx;
    let Inst{20-18} = idx;
    let Inst{17-16} = 0b10;
  }
  def vi16to64 : SIMDSMov<1, ".h", GPR64, VectorIndexH> {
    bits<3> idx;
    let Inst{20-18} = idx;
    let Inst{17-16} = 0b10;
  }
  def vi32to64 : SIMDSMov<1, ".s", GPR64, VectorIndexS> {
    bits<2> idx;
    let Inst{20-19} = idx;
    let Inst{18-16} = 0b100;
  }
}

multiclass UMov {
  def vi8 : SIMDUMov<0, ".b", v16i8, GPR32, VectorIndexB> {
    bits<4> idx;
    let Inst{20-17} = idx;
    let Inst{16} = 1;
  }
  def vi16 : SIMDUMov<0, ".h", v8i16, GPR32, VectorIndexH> {
    bits<3> idx;
    let Inst{20-18} = idx;
    let Inst{17-16} = 0b10;
  }
  def vi32 : SIMDUMov<0, ".s", v4i32, GPR32, VectorIndexS> {
    bits<2> idx;
    let Inst{20-19} = idx;
    let Inst{18-16} = 0b100;
  }
  def vi64 : SIMDUMov<1, ".d", v2i64, GPR64, VectorIndexD> {
    bits<1> idx;
    let Inst{20} = idx;
    let Inst{19-16} = 0b1000;
  }
  def : SIMDMovAlias<"mov", ".s",
                     !cast<Instruction>(NAME#"vi32"),
                     GPR32, VectorIndexS>;
  def : SIMDMovAlias<"mov", ".d",
                     !cast<Instruction>(NAME#"vi64"),
                     GPR64, VectorIndexD>;
}

class SIMDInsFromMain<string size, ValueType vectype,
                      RegisterClass regtype, Operand idxtype>
  : BaseSIMDInsDup<1, 0, (outs V128:$dst),
                   (ins V128:$Rd, idxtype:$idx, regtype:$Rn), "ins",
                   "{\t$Rd" # size # "$idx, $Rn" #
                   "|" # size # "\t$Rd$idx, $Rn}",
                   "$Rd = $dst",
            [(set V128:$dst,
              (vector_insert (vectype V128:$Rd), regtype:$Rn, idxtype:$idx))]> {
  let Inst{14-11} = 0b0011;
}

class SIMDInsFromElement<string size, ValueType vectype,
                         ValueType elttype, Operand idxtype>
  : BaseSIMDInsDup<1, 1, (outs V128:$dst),
                   (ins V128:$Rd, idxtype:$idx, V128:$Rn, idxtype:$idx2), "ins",
                   "{\t$Rd" # size # "$idx, $Rn" # size # "$idx2" #
                   "|" # size # "\t$Rd$idx, $Rn$idx2}",
                   "$Rd = $dst",
         [(set V128:$dst,
               (vector_insert
                 (vectype V128:$Rd),
                 (elttype (vector_extract (vectype V128:$Rn), idxtype:$idx2)),
                 idxtype:$idx))]>;

class SIMDInsMainMovAlias<string size, Instruction inst,
                          RegisterClass regtype, Operand idxtype>
    : InstAlias<"mov" # "{\t$dst" # size # "$idx, $src" #
                        "|" # size #"\t$dst$idx, $src}",
                (inst V128:$dst, idxtype:$idx, regtype:$src)>;
class SIMDInsElementMovAlias<string size, Instruction inst,
                             Operand idxtype>
    : InstAlias<"mov" # "{\t$dst" # size # "$idx, $src" # size # "$idx2" #
                      # "|" # size #"\t$dst$idx, $src$idx2}",
                (inst V128:$dst, idxtype:$idx, V128:$src, idxtype:$idx2)>;


multiclass SIMDIns {
  def vi8gpr : SIMDInsFromMain<".b", v16i8, GPR32, VectorIndexB> {
    bits<4> idx;
    let Inst{20-17} = idx;
    let Inst{16} = 1;
  }
  def vi16gpr : SIMDInsFromMain<".h", v8i16, GPR32, VectorIndexH> {
    bits<3> idx;
    let Inst{20-18} = idx;
    let Inst{17-16} = 0b10;
  }
  def vi32gpr : SIMDInsFromMain<".s", v4i32, GPR32, VectorIndexS> {
    bits<2> idx;
    let Inst{20-19} = idx;
    let Inst{18-16} = 0b100;
  }
  def vi64gpr : SIMDInsFromMain<".d", v2i64, GPR64, VectorIndexD> {
    bits<1> idx;
    let Inst{20} = idx;
    let Inst{19-16} = 0b1000;
  }

  def vi8lane : SIMDInsFromElement<".b", v16i8, i32, VectorIndexB> {
    bits<4> idx;
    bits<4> idx2;
    let Inst{20-17} = idx;
    let Inst{16} = 1;
    let Inst{14-11} = idx2;
  }
  def vi16lane : SIMDInsFromElement<".h", v8i16, i32, VectorIndexH> {
    bits<3> idx;
    bits<3> idx2;
    let Inst{20-18} = idx;
    let Inst{17-16} = 0b10;
    let Inst{14-12} = idx2;
    let Inst{11} = {?};
  }
  def vi32lane : SIMDInsFromElement<".s", v4i32, i32, VectorIndexS> {
    bits<2> idx;
    bits<2> idx2;
    let Inst{20-19} = idx;
    let Inst{18-16} = 0b100;
    let Inst{14-13} = idx2;
    let Inst{12-11} = {?,?};
  }
  def vi64lane : SIMDInsFromElement<".d", v2i64, i64, VectorIndexD> {
    bits<1> idx;
    bits<1> idx2;
    let Inst{20} = idx;
    let Inst{19-16} = 0b1000;
    let Inst{14} = idx2;
    let Inst{13-11} = {?,?,?};
  }

  // For all forms of the INS instruction, the "mov" mnemonic is the
  // preferred alias. Why they didn't just call the instruction "mov" in
  // the first place is a very good question indeed...
  def : SIMDInsMainMovAlias<".b", !cast<Instruction>(NAME#"vi8gpr"),
                         GPR32, VectorIndexB>;
  def : SIMDInsMainMovAlias<".h", !cast<Instruction>(NAME#"vi16gpr"),
                         GPR32, VectorIndexH>;
  def : SIMDInsMainMovAlias<".s", !cast<Instruction>(NAME#"vi32gpr"),
                         GPR32, VectorIndexS>;
  def : SIMDInsMainMovAlias<".d", !cast<Instruction>(NAME#"vi64gpr"),
                         GPR64, VectorIndexD>;

  def : SIMDInsElementMovAlias<".b", !cast<Instruction>(NAME#"vi8lane"),
                         VectorIndexB>;
  def : SIMDInsElementMovAlias<".h", !cast<Instruction>(NAME#"vi16lane"),
                         VectorIndexH>;
  def : SIMDInsElementMovAlias<".s", !cast<Instruction>(NAME#"vi32lane"),
                         VectorIndexS>;
  def : SIMDInsElementMovAlias<".d", !cast<Instruction>(NAME#"vi64lane"),
                         VectorIndexD>;
}

//----------------------------------------------------------------------------
// AdvSIMD TBL/TBX
//----------------------------------------------------------------------------

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
class BaseSIMDTableLookup<bit Q, bits<2> len, bit op, RegisterOperand vectype,
                          RegisterOperand listtype, string asm, string kind>
  : I<(outs vectype:$Vd), (ins listtype:$Vn, vectype:$Vm), asm,
       "\t$Vd" # kind # ", $Vn, $Vm" # kind, "", []>,
    Sched<[WriteV]> {
  bits<5> Vd;
  bits<5> Vn;
  bits<5> Vm;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29-21} = 0b001110000;
  let Inst{20-16} = Vm;
  let Inst{15}    = 0;
  let Inst{14-13} = len;
  let Inst{12}    = op;
  let Inst{11-10} = 0b00;
  let Inst{9-5}   = Vn;
  let Inst{4-0}   = Vd;
}

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
class BaseSIMDTableLookupTied<bit Q, bits<2> len, bit op, RegisterOperand vectype,
                          RegisterOperand listtype, string asm, string kind>
  : I<(outs vectype:$dst), (ins vectype:$Vd, listtype:$Vn, vectype:$Vm), asm,
       "\t$Vd" # kind # ", $Vn, $Vm" # kind, "$Vd = $dst", []>,
    Sched<[WriteV]> {
  bits<5> Vd;
  bits<5> Vn;
  bits<5> Vm;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29-21} = 0b001110000;
  let Inst{20-16} = Vm;
  let Inst{15}    = 0;
  let Inst{14-13} = len;
  let Inst{12}    = op;
  let Inst{11-10} = 0b00;
  let Inst{9-5}   = Vn;
  let Inst{4-0}   = Vd;
}

class SIMDTableLookupAlias<string asm, Instruction inst,
                          RegisterOperand vectype, RegisterOperand listtype>
    : InstAlias<!strconcat(asm, "\t$dst, $lst, $index"),
                (inst vectype:$dst, listtype:$lst, vectype:$index), 0>;

multiclass SIMDTableLookup<bit op, string asm> {
  def v8i8One   : BaseSIMDTableLookup<0, 0b00, op, V64, VecListOne16b,
                                      asm, ".8b">;
  def v8i8Two   : BaseSIMDTableLookup<0, 0b01, op, V64, VecListTwo16b,
                                      asm, ".8b">;
  def v8i8Three : BaseSIMDTableLookup<0, 0b10, op, V64, VecListThree16b,
                                      asm, ".8b">;
  def v8i8Four  : BaseSIMDTableLookup<0, 0b11, op, V64, VecListFour16b,
                                      asm, ".8b">;
  def v16i8One  : BaseSIMDTableLookup<1, 0b00, op, V128, VecListOne16b,
                                      asm, ".16b">;
  def v16i8Two  : BaseSIMDTableLookup<1, 0b01, op, V128, VecListTwo16b,
                                      asm, ".16b">;
  def v16i8Three: BaseSIMDTableLookup<1, 0b10, op, V128, VecListThree16b,
                                      asm, ".16b">;
  def v16i8Four : BaseSIMDTableLookup<1, 0b11, op, V128, VecListFour16b,
                                      asm, ".16b">;

  def : SIMDTableLookupAlias<asm # ".8b",
                         !cast<Instruction>(NAME#"v8i8One"),
                         V64, VecListOne128>;
  def : SIMDTableLookupAlias<asm # ".8b",
                         !cast<Instruction>(NAME#"v8i8Two"),
                         V64, VecListTwo128>;
  def : SIMDTableLookupAlias<asm # ".8b",
                         !cast<Instruction>(NAME#"v8i8Three"),
                         V64, VecListThree128>;
  def : SIMDTableLookupAlias<asm # ".8b",
                         !cast<Instruction>(NAME#"v8i8Four"),
                         V64, VecListFour128>;
  def : SIMDTableLookupAlias<asm # ".16b",
                         !cast<Instruction>(NAME#"v16i8One"),
                         V128, VecListOne128>;
  def : SIMDTableLookupAlias<asm # ".16b",
                         !cast<Instruction>(NAME#"v16i8Two"),
                         V128, VecListTwo128>;
  def : SIMDTableLookupAlias<asm # ".16b",
                         !cast<Instruction>(NAME#"v16i8Three"),
                         V128, VecListThree128>;
  def : SIMDTableLookupAlias<asm # ".16b",
                         !cast<Instruction>(NAME#"v16i8Four"),
                         V128, VecListFour128>;
}

multiclass SIMDTableLookupTied<bit op, string asm> {
  def v8i8One   : BaseSIMDTableLookupTied<0, 0b00, op, V64, VecListOne16b,
                                      asm, ".8b">;
  def v8i8Two   : BaseSIMDTableLookupTied<0, 0b01, op, V64, VecListTwo16b,
                                      asm, ".8b">;
  def v8i8Three : BaseSIMDTableLookupTied<0, 0b10, op, V64, VecListThree16b,
                                      asm, ".8b">;
  def v8i8Four  : BaseSIMDTableLookupTied<0, 0b11, op, V64, VecListFour16b,
                                      asm, ".8b">;
  def v16i8One  : BaseSIMDTableLookupTied<1, 0b00, op, V128, VecListOne16b,
                                      asm, ".16b">;
  def v16i8Two  : BaseSIMDTableLookupTied<1, 0b01, op, V128, VecListTwo16b,
                                      asm, ".16b">;
  def v16i8Three: BaseSIMDTableLookupTied<1, 0b10, op, V128, VecListThree16b,
                                      asm, ".16b">;
  def v16i8Four : BaseSIMDTableLookupTied<1, 0b11, op, V128, VecListFour16b,
                                      asm, ".16b">;

  def : SIMDTableLookupAlias<asm # ".8b",
                         !cast<Instruction>(NAME#"v8i8One"),
                         V64, VecListOne128>;
  def : SIMDTableLookupAlias<asm # ".8b",
                         !cast<Instruction>(NAME#"v8i8Two"),
                         V64, VecListTwo128>;
  def : SIMDTableLookupAlias<asm # ".8b",
                         !cast<Instruction>(NAME#"v8i8Three"),
                         V64, VecListThree128>;
  def : SIMDTableLookupAlias<asm # ".8b",
                         !cast<Instruction>(NAME#"v8i8Four"),
                         V64, VecListFour128>;
  def : SIMDTableLookupAlias<asm # ".16b",
                         !cast<Instruction>(NAME#"v16i8One"),
                         V128, VecListOne128>;
  def : SIMDTableLookupAlias<asm # ".16b",
                         !cast<Instruction>(NAME#"v16i8Two"),
                         V128, VecListTwo128>;
  def : SIMDTableLookupAlias<asm # ".16b",
                         !cast<Instruction>(NAME#"v16i8Three"),
                         V128, VecListThree128>;
  def : SIMDTableLookupAlias<asm # ".16b",
                         !cast<Instruction>(NAME#"v16i8Four"),
                         V128, VecListFour128>;
}


//----------------------------------------------------------------------------
// AdvSIMD scalar CPY
//----------------------------------------------------------------------------
let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDScalarCPY<RegisterClass regtype, RegisterOperand vectype,
                        string kind, Operand idxtype>
  : I<(outs regtype:$dst), (ins vectype:$src, idxtype:$idx), "mov",
       "{\t$dst, $src" # kind # "$idx" #
       "|\t$dst, $src$idx}", "", []>,
    Sched<[WriteV]> {
  bits<5> dst;
  bits<5> src;
  let Inst{31-21} = 0b01011110000;
  let Inst{15-10} = 0b000001;
  let Inst{9-5}   = src;
  let Inst{4-0}   = dst;
}

class SIMDScalarCPYAlias<string asm, string size, Instruction inst,
      RegisterClass regtype, RegisterOperand vectype, Operand idxtype>
    : InstAlias<asm # "{\t$dst, $src" # size # "$index" #
                    # "|\t$dst, $src$index}",
                (inst regtype:$dst, vectype:$src, idxtype:$index), 0>;


multiclass SIMDScalarCPY<string asm> {
  def i8  : BaseSIMDScalarCPY<FPR8,  V128, ".b", VectorIndexB> {
    bits<4> idx;
    let Inst{20-17} = idx;
    let Inst{16} = 1;
  }
  def i16 : BaseSIMDScalarCPY<FPR16, V128, ".h", VectorIndexH> {
    bits<3> idx;
    let Inst{20-18} = idx;
    let Inst{17-16} = 0b10;
  }
  def i32 : BaseSIMDScalarCPY<FPR32, V128, ".s", VectorIndexS> {
    bits<2> idx;
    let Inst{20-19} = idx;
    let Inst{18-16} = 0b100;
  }
  def i64 : BaseSIMDScalarCPY<FPR64, V128, ".d", VectorIndexD> {
    bits<1> idx;
    let Inst{20} = idx;
    let Inst{19-16} = 0b1000;
  }

  def : Pat<(v1i64 (scalar_to_vector (i64 (vector_extract (v2i64 V128:$src),
                                                          VectorIndexD:$idx)))),
            (!cast<Instruction>(NAME # i64) V128:$src, VectorIndexD:$idx)>;

  // 'DUP' mnemonic aliases.
  def : SIMDScalarCPYAlias<"dup", ".b",
                           !cast<Instruction>(NAME#"i8"),
                           FPR8, V128, VectorIndexB>;
  def : SIMDScalarCPYAlias<"dup", ".h",
                           !cast<Instruction>(NAME#"i16"),
                           FPR16, V128, VectorIndexH>;
  def : SIMDScalarCPYAlias<"dup", ".s",
                           !cast<Instruction>(NAME#"i32"),
                           FPR32, V128, VectorIndexS>;
  def : SIMDScalarCPYAlias<"dup", ".d",
                           !cast<Instruction>(NAME#"i64"),
                           FPR64, V128, VectorIndexD>;
}

//----------------------------------------------------------------------------
// AdvSIMD modified immediate instructions
//----------------------------------------------------------------------------

class BaseSIMDModifiedImm<bit Q, bit op, bit op2, dag oops, dag iops,
                          string asm, string op_string,
                          string cstr, list<dag> pattern>
  : I<oops, iops, asm, op_string, cstr, pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<8> imm8;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = op;
  let Inst{28-19} = 0b0111100000;
  let Inst{18-16} = imm8{7-5};
  let Inst{11} = op2;
  let Inst{10} = 1;
  let Inst{9-5}   = imm8{4-0};
  let Inst{4-0}   = Rd;
}

class BaseSIMDModifiedImmVector<bit Q, bit op, bit op2, RegisterOperand vectype,
                                Operand immtype, dag opt_shift_iop,
                                string opt_shift, string asm, string kind,
                                list<dag> pattern>
  : BaseSIMDModifiedImm<Q, op, op2, (outs vectype:$Rd),
                        !con((ins immtype:$imm8), opt_shift_iop), asm,
                        "{\t$Rd" # kind # ", $imm8" # opt_shift #
                        "|" # kind # "\t$Rd, $imm8" # opt_shift # "}",
                        "", pattern> {
  let DecoderMethod = "DecodeModImmInstruction";
}

class BaseSIMDModifiedImmVectorTied<bit Q, bit op, RegisterOperand vectype,
                                Operand immtype, dag opt_shift_iop,
                                string opt_shift, string asm, string kind,
                                list<dag> pattern>
  : BaseSIMDModifiedImm<Q, op, 0, (outs vectype:$dst),
                        !con((ins vectype:$Rd, immtype:$imm8), opt_shift_iop),
                        asm, "{\t$Rd" # kind # ", $imm8" # opt_shift #
                             "|" # kind # "\t$Rd, $imm8" # opt_shift # "}",
                        "$Rd = $dst", pattern> {
  let DecoderMethod = "DecodeModImmTiedInstruction";
}

class BaseSIMDModifiedImmVectorShift<bit Q, bit op, bits<2> b15_b12,
                                     RegisterOperand vectype, string asm,
                                     string kind, list<dag> pattern>
  : BaseSIMDModifiedImmVector<Q, op, 0, vectype, imm0_255,
                              (ins logical_vec_shift:$shift),
                              "$shift", asm, kind, pattern> {
  bits<2> shift;
  let Inst{15}    = b15_b12{1};
  let Inst{14-13} = shift;
  let Inst{12}    = b15_b12{0};
}

class BaseSIMDModifiedImmVectorShiftTied<bit Q, bit op, bits<2> b15_b12,
                                     RegisterOperand vectype, string asm,
                                     string kind, list<dag> pattern>
  : BaseSIMDModifiedImmVectorTied<Q, op, vectype, imm0_255,
                              (ins logical_vec_shift:$shift),
                              "$shift", asm, kind, pattern> {
  bits<2> shift;
  let Inst{15}    = b15_b12{1};
  let Inst{14-13} = shift;
  let Inst{12}    = b15_b12{0};
}


class BaseSIMDModifiedImmVectorShiftHalf<bit Q, bit op, bits<2> b15_b12,
                                         RegisterOperand vectype, string asm,
                                         string kind, list<dag> pattern>
  : BaseSIMDModifiedImmVector<Q, op, 0, vectype, imm0_255,
                              (ins logical_vec_hw_shift:$shift),
                              "$shift", asm, kind, pattern> {
  bits<2> shift;
  let Inst{15} = b15_b12{1};
  let Inst{14} = 0;
  let Inst{13} = shift{0};
  let Inst{12} = b15_b12{0};
}

class BaseSIMDModifiedImmVectorShiftHalfTied<bit Q, bit op, bits<2> b15_b12,
                                         RegisterOperand vectype, string asm,
                                         string kind, list<dag> pattern>
  : BaseSIMDModifiedImmVectorTied<Q, op, vectype, imm0_255,
                              (ins logical_vec_hw_shift:$shift),
                              "$shift", asm, kind, pattern> {
  bits<2> shift;
  let Inst{15} = b15_b12{1};
  let Inst{14} = 0;
  let Inst{13} = shift{0};
  let Inst{12} = b15_b12{0};
}

multiclass SIMDModifiedImmVectorShift<bit op, bits<2> hw_cmode, bits<2> w_cmode,
                                      string asm> {
  def v4i16 : BaseSIMDModifiedImmVectorShiftHalf<0, op, hw_cmode, V64,
                                                 asm, ".4h", []>;
  def v8i16 : BaseSIMDModifiedImmVectorShiftHalf<1, op, hw_cmode, V128,
                                                 asm, ".8h", []>;

  def v2i32 : BaseSIMDModifiedImmVectorShift<0, op, w_cmode, V64,
                                             asm, ".2s", []>;
  def v4i32 : BaseSIMDModifiedImmVectorShift<1, op, w_cmode, V128,
                                             asm, ".4s", []>;
}

multiclass SIMDModifiedImmVectorShiftTied<bit op, bits<2> hw_cmode,
                                      bits<2> w_cmode, string asm,
                                      SDNode OpNode> {
  def v4i16 : BaseSIMDModifiedImmVectorShiftHalfTied<0, op, hw_cmode, V64,
                                                 asm, ".4h",
             [(set (v4i16 V64:$dst), (OpNode V64:$Rd,
                                             imm0_255:$imm8,
                                             (i32 imm:$shift)))]>;
  def v8i16 : BaseSIMDModifiedImmVectorShiftHalfTied<1, op, hw_cmode, V128,
                                                 asm, ".8h",
             [(set (v8i16 V128:$dst), (OpNode V128:$Rd,
                                              imm0_255:$imm8,
                                              (i32 imm:$shift)))]>;

  def v2i32 : BaseSIMDModifiedImmVectorShiftTied<0, op, w_cmode, V64,
                                             asm, ".2s",
             [(set (v2i32 V64:$dst), (OpNode V64:$Rd,
                                             imm0_255:$imm8,
                                             (i32 imm:$shift)))]>;
  def v4i32 : BaseSIMDModifiedImmVectorShiftTied<1, op, w_cmode, V128,
                                             asm, ".4s",
             [(set (v4i32 V128:$dst), (OpNode V128:$Rd,
                                              imm0_255:$imm8,
                                              (i32 imm:$shift)))]>;
}

class SIMDModifiedImmMoveMSL<bit Q, bit op, bits<4> cmode,
                             RegisterOperand vectype, string asm,
                             string kind, list<dag> pattern>
  : BaseSIMDModifiedImmVector<Q, op, 0, vectype, imm0_255,
                              (ins move_vec_shift:$shift),
                              "$shift", asm, kind, pattern> {
  bits<1> shift;
  let Inst{15-13} = cmode{3-1};
  let Inst{12}    = shift;
}

class SIMDModifiedImmVectorNoShift<bit Q, bit op, bit op2, bits<4> cmode,
                                   RegisterOperand vectype,
                                   Operand imm_type, string asm,
                                   string kind, list<dag> pattern>
  : BaseSIMDModifiedImmVector<Q, op, op2, vectype, imm_type, (ins), "",
                              asm, kind, pattern> {
  let Inst{15-12} = cmode;
}

class SIMDModifiedImmScalarNoShift<bit Q, bit op, bits<4> cmode, string asm,
                                   list<dag> pattern>
  : BaseSIMDModifiedImm<Q, op, 0, (outs FPR64:$Rd), (ins simdimmtype10:$imm8), asm,
                        "\t$Rd, $imm8", "", pattern> {
  let Inst{15-12} = cmode;
  let DecoderMethod = "DecodeModImmInstruction";
}

//----------------------------------------------------------------------------
// AdvSIMD indexed element
//----------------------------------------------------------------------------

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDIndexed<bit Q, bit U, bit Scalar, bits<2> size, bits<4> opc,
                      RegisterOperand dst_reg, RegisterOperand lhs_reg,
                      RegisterOperand rhs_reg, Operand vec_idx, string asm,
                      string apple_kind, string dst_kind, string lhs_kind,
                      string rhs_kind, list<dag> pattern>
  : I<(outs dst_reg:$Rd), (ins lhs_reg:$Rn, rhs_reg:$Rm, vec_idx:$idx),
      asm,
      "{\t$Rd" # dst_kind # ", $Rn" # lhs_kind # ", $Rm" # rhs_kind # "$idx" #
      "|" # apple_kind # "\t$Rd, $Rn, $Rm$idx}", "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;

  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28}    = Scalar;
  let Inst{27-24} = 0b1111;
  let Inst{23-22} = size;
  // Bit 21 must be set by the derived class.
  let Inst{20-16} = Rm;
  let Inst{15-12} = opc;
  // Bit 11 must be set by the derived class.
  let Inst{10}    = 0;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDIndexedTied<bit Q, bit U, bit Scalar, bits<2> size, bits<4> opc,
                      RegisterOperand dst_reg, RegisterOperand lhs_reg,
                      RegisterOperand rhs_reg, Operand vec_idx, string asm,
                      string apple_kind, string dst_kind, string lhs_kind,
                      string rhs_kind, list<dag> pattern>
  : I<(outs dst_reg:$dst),
      (ins dst_reg:$Rd, lhs_reg:$Rn, rhs_reg:$Rm, vec_idx:$idx), asm,
      "{\t$Rd" # dst_kind # ", $Rn" # lhs_kind # ", $Rm" # rhs_kind # "$idx" #
      "|" # apple_kind # "\t$Rd, $Rn, $Rm$idx}", "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;

  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28}    = Scalar;
  let Inst{27-24} = 0b1111;
  let Inst{23-22} = size;
  // Bit 21 must be set by the derived class.
  let Inst{20-16} = Rm;
  let Inst{15-12} = opc;
  // Bit 11 must be set by the derived class.
  let Inst{10}    = 0;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

// ARMv8.2-A Dot Product Instructions (Indexed)
class BaseSIMDThreeSameVectorDotIndex<bit Q, bit U, string asm, string dst_kind,
                                      string lhs_kind, string rhs_kind,
                                      RegisterOperand RegType,
                                      ValueType AccumType, ValueType InputType,
                                      SDPatternOperator OpNode> :
        BaseSIMDIndexedTied<Q, U, 0b0, 0b10, 0b1110, RegType, RegType, V128,
                            VectorIndexS, asm, "", dst_kind, lhs_kind, rhs_kind,
        [(set (AccumType RegType:$dst),
              (AccumType (OpNode (AccumType RegType:$Rd),
                                 (InputType RegType:$Rn),
                                 (InputType (bitconvert (AccumType
                                    (AArch64duplane32 (v4i32 V128:$Rm),
                                        VectorIndexS:$idx)))))))]> {
  bits<2> idx;
  let Inst{21}    = idx{0};  // L
  let Inst{11}    = idx{1};  // H
}

multiclass SIMDThreeSameVectorDotIndex<bit U, string asm,
                                       SDPatternOperator OpNode> {
  def v8i8  : BaseSIMDThreeSameVectorDotIndex<0, U, asm, ".2s", ".8b", ".4b",
                                              V64, v2i32, v8i8, OpNode>;
  def v16i8 : BaseSIMDThreeSameVectorDotIndex<1, U, asm, ".4s", ".16b", ".4b",
                                              V128, v4i32, v16i8, OpNode>;
}

// ARMv8.2-A Fused Multiply Add-Long Instructions (Indexed)
class BaseSIMDThreeSameVectorFMLIndex<bit Q, bit U, bits<4> opc, string asm,
                                      string dst_kind, string lhs_kind,
                                      string rhs_kind, RegisterOperand RegType,
                                      ValueType AccumType, ValueType InputType,
                                      SDPatternOperator OpNode> :
        BaseSIMDIndexedTied<Q, U, 0, 0b10, opc, RegType, RegType, V128,
                            VectorIndexH, asm, "", dst_kind, lhs_kind, rhs_kind,
          [(set (AccumType RegType:$dst),
                (AccumType (OpNode (AccumType RegType:$Rd),
                                   (InputType RegType:$Rn),
                                   (InputType (AArch64duplane16 (v8f16 V128:$Rm),
                                                VectorIndexH:$idx)))))]> {
  // idx = H:L:M
  bits<3> idx;
  let Inst{11} = idx{2}; // H
  let Inst{21} = idx{1}; // L
  let Inst{20} = idx{0}; // M
}

multiclass SIMDThreeSameVectorFMLIndex<bit U, bits<4> opc, string asm,
                                       SDPatternOperator OpNode> {
  def v4f16 : BaseSIMDThreeSameVectorFMLIndex<0, U, opc, asm, ".2s", ".2h", ".h",
                                              V64, v2f32, v4f16, OpNode>;
  def v8f16 : BaseSIMDThreeSameVectorFMLIndex<1, U, opc, asm, ".4s", ".4h", ".h",
                                              V128, v4f32, v8f16, OpNode>;
}

multiclass SIMDFPIndexed<bit U, bits<4> opc, string asm,
                         SDPatternOperator OpNode> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4i16_indexed : BaseSIMDIndexed<0, U, 0, 0b00, opc,
                                      V64, V64,
                                      V128_lo, VectorIndexH,
                                      asm, ".4h", ".4h", ".4h", ".h",
    [(set (v4f16 V64:$Rd),
        (OpNode (v4f16 V64:$Rn),
         (v4f16 (AArch64duplane16 (v8f16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v8i16_indexed : BaseSIMDIndexed<1, U, 0, 0b00, opc,
                                      V128, V128,
                                      V128_lo, VectorIndexH,
                                      asm, ".8h", ".8h", ".8h", ".h",
    [(set (v8f16 V128:$Rd),
        (OpNode (v8f16 V128:$Rn),
         (v8f16 (AArch64duplane16 (v8f16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }
  } // Predicates = [HasNEON, HasFullFP16]

  def v2i32_indexed : BaseSIMDIndexed<0, U, 0, 0b10, opc,
                                      V64, V64,
                                      V128, VectorIndexS,
                                      asm, ".2s", ".2s", ".2s", ".s",
    [(set (v2f32 V64:$Rd),
        (OpNode (v2f32 V64:$Rn),
         (v2f32 (AArch64duplane32 (v4f32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v4i32_indexed : BaseSIMDIndexed<1, U, 0, 0b10, opc,
                                      V128, V128,
                                      V128, VectorIndexS,
                                      asm, ".4s", ".4s", ".4s", ".s",
    [(set (v4f32 V128:$Rd),
        (OpNode (v4f32 V128:$Rn),
         (v4f32 (AArch64duplane32 (v4f32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v2i64_indexed : BaseSIMDIndexed<1, U, 0, 0b11, opc,
                                      V128, V128,
                                      V128, VectorIndexD,
                                      asm, ".2d", ".2d", ".2d", ".d",
    [(set (v2f64 V128:$Rd),
        (OpNode (v2f64 V128:$Rn),
         (v2f64 (AArch64duplane64 (v2f64 V128:$Rm), VectorIndexD:$idx))))]> {
    bits<1> idx;
    let Inst{11} = idx{0};
    let Inst{21} = 0;
  }

  let Predicates = [HasNEON, HasFullFP16] in {
  def v1i16_indexed : BaseSIMDIndexed<1, U, 1, 0b00, opc,
                                      FPR16Op, FPR16Op, V128_lo, VectorIndexH,
                                      asm, ".h", "", "", ".h",
    [(set (f16 FPR16Op:$Rd),
          (OpNode (f16 FPR16Op:$Rn),
                  (f16 (vector_extract (v8f16 V128_lo:$Rm),
                                       VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }
  } // Predicates = [HasNEON, HasFullFP16]

  def v1i32_indexed : BaseSIMDIndexed<1, U, 1, 0b10, opc,
                                      FPR32Op, FPR32Op, V128, VectorIndexS,
                                      asm, ".s", "", "", ".s",
    [(set (f32 FPR32Op:$Rd),
          (OpNode (f32 FPR32Op:$Rn),
                  (f32 (vector_extract (v4f32 V128:$Rm),
                                       VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v1i64_indexed : BaseSIMDIndexed<1, U, 1, 0b11, opc,
                                      FPR64Op, FPR64Op, V128, VectorIndexD,
                                      asm, ".d", "", "", ".d",
    [(set (f64 FPR64Op:$Rd),
          (OpNode (f64 FPR64Op:$Rn),
                  (f64 (vector_extract (v2f64 V128:$Rm),
                                       VectorIndexD:$idx))))]> {
    bits<1> idx;
    let Inst{11} = idx{0};
    let Inst{21} = 0;
  }
}

multiclass SIMDFPIndexedTiedPatterns<string INST, SDPatternOperator OpNode> {
  // 2 variants for the .2s version: DUPLANE from 128-bit and DUP scalar.
  def : Pat<(v2f32 (OpNode (v2f32 V64:$Rd), (v2f32 V64:$Rn),
                           (AArch64duplane32 (v4f32 V128:$Rm),
                                           VectorIndexS:$idx))),
            (!cast<Instruction>(INST # v2i32_indexed)
                V64:$Rd, V64:$Rn, V128:$Rm, VectorIndexS:$idx)>;
  def : Pat<(v2f32 (OpNode (v2f32 V64:$Rd), (v2f32 V64:$Rn),
                           (AArch64dup (f32 FPR32Op:$Rm)))),
            (!cast<Instruction>(INST # "v2i32_indexed") V64:$Rd, V64:$Rn,
                (SUBREG_TO_REG (i32 0), FPR32Op:$Rm, ssub), (i64 0))>;


  // 2 variants for the .4s version: DUPLANE from 128-bit and DUP scalar.
  def : Pat<(v4f32 (OpNode (v4f32 V128:$Rd), (v4f32 V128:$Rn),
                           (AArch64duplane32 (v4f32 V128:$Rm),
                                           VectorIndexS:$idx))),
            (!cast<Instruction>(INST # "v4i32_indexed")
                V128:$Rd, V128:$Rn, V128:$Rm, VectorIndexS:$idx)>;
  def : Pat<(v4f32 (OpNode (v4f32 V128:$Rd), (v4f32 V128:$Rn),
                           (AArch64dup (f32 FPR32Op:$Rm)))),
            (!cast<Instruction>(INST # "v4i32_indexed") V128:$Rd, V128:$Rn,
                (SUBREG_TO_REG (i32 0), FPR32Op:$Rm, ssub), (i64 0))>;

  // 2 variants for the .2d version: DUPLANE from 128-bit and DUP scalar.
  def : Pat<(v2f64 (OpNode (v2f64 V128:$Rd), (v2f64 V128:$Rn),
                           (AArch64duplane64 (v2f64 V128:$Rm),
                                           VectorIndexD:$idx))),
            (!cast<Instruction>(INST # "v2i64_indexed")
                V128:$Rd, V128:$Rn, V128:$Rm, VectorIndexS:$idx)>;
  def : Pat<(v2f64 (OpNode (v2f64 V128:$Rd), (v2f64 V128:$Rn),
                           (AArch64dup (f64 FPR64Op:$Rm)))),
            (!cast<Instruction>(INST # "v2i64_indexed") V128:$Rd, V128:$Rn,
                (SUBREG_TO_REG (i32 0), FPR64Op:$Rm, dsub), (i64 0))>;

  // 2 variants for 32-bit scalar version: extract from .2s or from .4s
  def : Pat<(f32 (OpNode (f32 FPR32:$Rd), (f32 FPR32:$Rn),
                         (vector_extract (v4f32 V128:$Rm), VectorIndexS:$idx))),
            (!cast<Instruction>(INST # "v1i32_indexed") FPR32:$Rd, FPR32:$Rn,
                V128:$Rm, VectorIndexS:$idx)>;
  def : Pat<(f32 (OpNode (f32 FPR32:$Rd), (f32 FPR32:$Rn),
                         (vector_extract (v2f32 V64:$Rm), VectorIndexS:$idx))),
            (!cast<Instruction>(INST # "v1i32_indexed") FPR32:$Rd, FPR32:$Rn,
                (SUBREG_TO_REG (i32 0), V64:$Rm, dsub), VectorIndexS:$idx)>;

  // 1 variant for 64-bit scalar version: extract from .1d or from .2d
  def : Pat<(f64 (OpNode (f64 FPR64:$Rd), (f64 FPR64:$Rn),
                         (vector_extract (v2f64 V128:$Rm), VectorIndexD:$idx))),
            (!cast<Instruction>(INST # "v1i64_indexed") FPR64:$Rd, FPR64:$Rn,
                V128:$Rm, VectorIndexD:$idx)>;
}

multiclass SIMDFPIndexedTied<bit U, bits<4> opc, string asm> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4i16_indexed : BaseSIMDIndexedTied<0, U, 0, 0b00, opc, V64, V64,
                                          V128_lo, VectorIndexH,
                                          asm, ".4h", ".4h", ".4h", ".h", []> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v8i16_indexed : BaseSIMDIndexedTied<1, U, 0, 0b00, opc,
                                          V128, V128,
                                          V128_lo, VectorIndexH,
                                          asm, ".8h", ".8h", ".8h", ".h", []> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }
  } // Predicates = [HasNEON, HasFullFP16]

  def v2i32_indexed : BaseSIMDIndexedTied<0, U, 0, 0b10, opc, V64, V64,
                                          V128, VectorIndexS,
                                          asm, ".2s", ".2s", ".2s", ".s", []> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v4i32_indexed : BaseSIMDIndexedTied<1, U, 0, 0b10, opc,
                                      V128, V128,
                                      V128, VectorIndexS,
                                      asm, ".4s", ".4s", ".4s", ".s", []> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v2i64_indexed : BaseSIMDIndexedTied<1, U, 0, 0b11, opc,
                                      V128, V128,
                                      V128, VectorIndexD,
                                      asm, ".2d", ".2d", ".2d", ".d", []> {
    bits<1> idx;
    let Inst{11} = idx{0};
    let Inst{21} = 0;
  }

  let Predicates = [HasNEON, HasFullFP16] in {
  def v1i16_indexed : BaseSIMDIndexedTied<1, U, 1, 0b00, opc,
                                      FPR16Op, FPR16Op, V128_lo, VectorIndexH,
                                      asm, ".h", "", "", ".h", []> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }
  } // Predicates = [HasNEON, HasFullFP16]

  def v1i32_indexed : BaseSIMDIndexedTied<1, U, 1, 0b10, opc,
                                      FPR32Op, FPR32Op, V128, VectorIndexS,
                                      asm, ".s", "", "", ".s", []> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v1i64_indexed : BaseSIMDIndexedTied<1, U, 1, 0b11, opc,
                                      FPR64Op, FPR64Op, V128, VectorIndexD,
                                      asm, ".d", "", "", ".d", []> {
    bits<1> idx;
    let Inst{11} = idx{0};
    let Inst{21} = 0;
  }
}

multiclass SIMDIndexedHS<bit U, bits<4> opc, string asm,
                         SDPatternOperator OpNode> {
  def v4i16_indexed : BaseSIMDIndexed<0, U, 0, 0b01, opc, V64, V64,
                                      V128_lo, VectorIndexH,
                                      asm, ".4h", ".4h", ".4h", ".h",
    [(set (v4i16 V64:$Rd),
        (OpNode (v4i16 V64:$Rn),
         (v4i16 (AArch64duplane16 (v8i16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v8i16_indexed : BaseSIMDIndexed<1, U, 0, 0b01, opc,
                                      V128, V128,
                                      V128_lo, VectorIndexH,
                                      asm, ".8h", ".8h", ".8h", ".h",
    [(set (v8i16 V128:$Rd),
       (OpNode (v8i16 V128:$Rn),
         (v8i16 (AArch64duplane16 (v8i16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v2i32_indexed : BaseSIMDIndexed<0, U, 0, 0b10, opc,
                                      V64, V64,
                                      V128, VectorIndexS,
                                      asm, ".2s", ".2s", ".2s",  ".s",
    [(set (v2i32 V64:$Rd),
       (OpNode (v2i32 V64:$Rn),
          (v2i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v4i32_indexed : BaseSIMDIndexed<1, U, 0, 0b10, opc,
                                      V128, V128,
                                      V128, VectorIndexS,
                                      asm, ".4s", ".4s", ".4s", ".s",
    [(set (v4i32 V128:$Rd),
       (OpNode (v4i32 V128:$Rn),
          (v4i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v1i16_indexed : BaseSIMDIndexed<1, U, 1, 0b01, opc,
                                      FPR16Op, FPR16Op, V128_lo, VectorIndexH,
                                      asm, ".h", "", "", ".h", []> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v1i32_indexed : BaseSIMDIndexed<1, U, 1, 0b10, opc,
                                      FPR32Op, FPR32Op, V128, VectorIndexS,
                                      asm, ".s", "", "", ".s",
      [(set (i32 FPR32Op:$Rd),
            (OpNode FPR32Op:$Rn,
                    (i32 (vector_extract (v4i32 V128:$Rm),
                                         VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }
}

multiclass SIMDVectorIndexedHS<bit U, bits<4> opc, string asm,
                               SDPatternOperator OpNode> {
  def v4i16_indexed : BaseSIMDIndexed<0, U, 0, 0b01, opc,
                                      V64, V64,
                                      V128_lo, VectorIndexH,
                                      asm, ".4h", ".4h", ".4h", ".h",
    [(set (v4i16 V64:$Rd),
        (OpNode (v4i16 V64:$Rn),
         (v4i16 (AArch64duplane16 (v8i16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v8i16_indexed : BaseSIMDIndexed<1, U, 0, 0b01, opc,
                                      V128, V128,
                                      V128_lo, VectorIndexH,
                                      asm, ".8h", ".8h", ".8h", ".h",
    [(set (v8i16 V128:$Rd),
       (OpNode (v8i16 V128:$Rn),
         (v8i16 (AArch64duplane16 (v8i16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v2i32_indexed : BaseSIMDIndexed<0, U, 0, 0b10, opc,
                                      V64, V64,
                                      V128, VectorIndexS,
                                      asm, ".2s", ".2s", ".2s", ".s",
    [(set (v2i32 V64:$Rd),
       (OpNode (v2i32 V64:$Rn),
          (v2i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v4i32_indexed : BaseSIMDIndexed<1, U, 0, 0b10, opc,
                                      V128, V128,
                                      V128, VectorIndexS,
                                      asm, ".4s", ".4s", ".4s", ".s",
    [(set (v4i32 V128:$Rd),
       (OpNode (v4i32 V128:$Rn),
          (v4i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }
}

multiclass SIMDVectorIndexedHSTied<bit U, bits<4> opc, string asm,
                                   SDPatternOperator OpNode> {
  def v4i16_indexed : BaseSIMDIndexedTied<0, U, 0, 0b01, opc, V64, V64,
                                          V128_lo, VectorIndexH,
                                          asm, ".4h", ".4h", ".4h", ".h",
    [(set (v4i16 V64:$dst),
        (OpNode (v4i16 V64:$Rd),(v4i16 V64:$Rn),
         (v4i16 (AArch64duplane16 (v8i16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v8i16_indexed : BaseSIMDIndexedTied<1, U, 0, 0b01, opc,
                                      V128, V128,
                                      V128_lo, VectorIndexH,
                                      asm, ".8h", ".8h", ".8h", ".h",
    [(set (v8i16 V128:$dst),
       (OpNode (v8i16 V128:$Rd), (v8i16 V128:$Rn),
         (v8i16 (AArch64duplane16 (v8i16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v2i32_indexed : BaseSIMDIndexedTied<0, U, 0, 0b10, opc,
                                      V64, V64,
                                      V128, VectorIndexS,
                                      asm, ".2s", ".2s", ".2s", ".s",
    [(set (v2i32 V64:$dst),
       (OpNode (v2i32 V64:$Rd), (v2i32 V64:$Rn),
          (v2i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v4i32_indexed : BaseSIMDIndexedTied<1, U, 0, 0b10, opc,
                                      V128, V128,
                                      V128, VectorIndexS,
                                      asm, ".4s", ".4s", ".4s", ".s",
    [(set (v4i32 V128:$dst),
       (OpNode (v4i32 V128:$Rd), (v4i32 V128:$Rn),
          (v4i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }
}

multiclass SIMDIndexedLongSD<bit U, bits<4> opc, string asm,
                             SDPatternOperator OpNode> {
  def v4i16_indexed : BaseSIMDIndexed<0, U, 0, 0b01, opc,
                                      V128, V64,
                                      V128_lo, VectorIndexH,
                                      asm, ".4s", ".4s", ".4h", ".h",
    [(set (v4i32 V128:$Rd),
        (OpNode (v4i16 V64:$Rn),
         (v4i16 (AArch64duplane16 (v8i16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v8i16_indexed : BaseSIMDIndexed<1, U, 0, 0b01, opc,
                                      V128, V128,
                                      V128_lo, VectorIndexH,
                                      asm#"2", ".4s", ".4s", ".8h", ".h",
    [(set (v4i32 V128:$Rd),
          (OpNode (extract_high_v8i16 V128:$Rn),
                  (extract_high_v8i16 (AArch64duplane16 (v8i16 V128_lo:$Rm),
                                                      VectorIndexH:$idx))))]> {

    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v2i32_indexed : BaseSIMDIndexed<0, U, 0, 0b10, opc,
                                      V128, V64,
                                      V128, VectorIndexS,
                                      asm, ".2d", ".2d", ".2s", ".s",
    [(set (v2i64 V128:$Rd),
        (OpNode (v2i32 V64:$Rn),
         (v2i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v4i32_indexed : BaseSIMDIndexed<1, U, 0, 0b10, opc,
                                      V128, V128,
                                      V128, VectorIndexS,
                                      asm#"2", ".2d", ".2d", ".4s", ".s",
    [(set (v2i64 V128:$Rd),
          (OpNode (extract_high_v4i32 V128:$Rn),
                  (extract_high_v4i32 (AArch64duplane32 (v4i32 V128:$Rm),
                                                      VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v1i32_indexed : BaseSIMDIndexed<1, U, 1, 0b01, opc,
                                      FPR32Op, FPR16Op, V128_lo, VectorIndexH,
                                      asm, ".h", "", "", ".h", []> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v1i64_indexed : BaseSIMDIndexed<1, U, 1, 0b10, opc,
                                      FPR64Op, FPR32Op, V128, VectorIndexS,
                                      asm, ".s", "", "", ".s", []> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }
}

multiclass SIMDIndexedLongSQDMLXSDTied<bit U, bits<4> opc, string asm,
                                       SDPatternOperator Accum> {
  def v4i16_indexed : BaseSIMDIndexedTied<0, U, 0, 0b01, opc,
                                      V128, V64,
                                      V128_lo, VectorIndexH,
                                      asm, ".4s", ".4s", ".4h", ".h",
    [(set (v4i32 V128:$dst),
          (Accum (v4i32 V128:$Rd),
                 (v4i32 (int_aarch64_neon_sqdmull
                             (v4i16 V64:$Rn),
                             (v4i16 (AArch64duplane16 (v8i16 V128_lo:$Rm),
                                                    VectorIndexH:$idx))))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  // FIXME: it would be nice to use the scalar (v1i32) instruction here, but an
  // intermediate EXTRACT_SUBREG would be untyped.
  def : Pat<(i32 (Accum (i32 FPR32Op:$Rd),
                (i32 (vector_extract (v4i32
                         (int_aarch64_neon_sqdmull (v4i16 V64:$Rn),
                             (v4i16 (AArch64duplane16 (v8i16 V128_lo:$Rm),
                                                    VectorIndexH:$idx)))),
                         (i64 0))))),
            (EXTRACT_SUBREG
                (!cast<Instruction>(NAME # v4i16_indexed)
                    (SUBREG_TO_REG (i32 0), FPR32Op:$Rd, ssub), V64:$Rn,
                    V128_lo:$Rm, VectorIndexH:$idx),
                ssub)>;

  def v8i16_indexed : BaseSIMDIndexedTied<1, U, 0, 0b01, opc,
                                      V128, V128,
                                      V128_lo, VectorIndexH,
                                      asm#"2", ".4s", ".4s", ".8h", ".h",
    [(set (v4i32 V128:$dst),
          (Accum (v4i32 V128:$Rd),
                 (v4i32 (int_aarch64_neon_sqdmull
                            (extract_high_v8i16 V128:$Rn),
                            (extract_high_v8i16
                                (AArch64duplane16 (v8i16 V128_lo:$Rm),
                                                VectorIndexH:$idx))))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v2i32_indexed : BaseSIMDIndexedTied<0, U, 0, 0b10, opc,
                                      V128, V64,
                                      V128, VectorIndexS,
                                      asm, ".2d", ".2d", ".2s", ".s",
    [(set (v2i64 V128:$dst),
        (Accum (v2i64 V128:$Rd),
               (v2i64 (int_aarch64_neon_sqdmull
                          (v2i32 V64:$Rn),
                          (v2i32 (AArch64duplane32 (v4i32 V128:$Rm),
                                                 VectorIndexS:$idx))))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v4i32_indexed : BaseSIMDIndexedTied<1, U, 0, 0b10, opc,
                                      V128, V128,
                                      V128, VectorIndexS,
                                      asm#"2", ".2d", ".2d", ".4s", ".s",
    [(set (v2i64 V128:$dst),
          (Accum (v2i64 V128:$Rd),
                 (v2i64 (int_aarch64_neon_sqdmull
                            (extract_high_v4i32 V128:$Rn),
                            (extract_high_v4i32
                                (AArch64duplane32 (v4i32 V128:$Rm),
                                                VectorIndexS:$idx))))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v1i32_indexed : BaseSIMDIndexedTied<1, U, 1, 0b01, opc,
                                      FPR32Op, FPR16Op, V128_lo, VectorIndexH,
                                      asm, ".h", "", "", ".h", []> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }


  def v1i64_indexed : BaseSIMDIndexedTied<1, U, 1, 0b10, opc,
                                      FPR64Op, FPR32Op, V128, VectorIndexS,
                                      asm, ".s", "", "", ".s",
    [(set (i64 FPR64Op:$dst),
          (Accum (i64 FPR64Op:$Rd),
                 (i64 (int_aarch64_neon_sqdmulls_scalar
                            (i32 FPR32Op:$Rn),
                            (i32 (vector_extract (v4i32 V128:$Rm),
                                                 VectorIndexS:$idx))))))]> {

    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }
}

multiclass SIMDVectorIndexedLongSD<bit U, bits<4> opc, string asm,
                                   SDPatternOperator OpNode> {
  let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {
  def v4i16_indexed : BaseSIMDIndexed<0, U, 0, 0b01, opc,
                                      V128, V64,
                                      V128_lo, VectorIndexH,
                                      asm, ".4s", ".4s", ".4h", ".h",
    [(set (v4i32 V128:$Rd),
        (OpNode (v4i16 V64:$Rn),
         (v4i16 (AArch64duplane16 (v8i16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v8i16_indexed : BaseSIMDIndexed<1, U, 0, 0b01, opc,
                                      V128, V128,
                                      V128_lo, VectorIndexH,
                                      asm#"2", ".4s", ".4s", ".8h", ".h",
    [(set (v4i32 V128:$Rd),
          (OpNode (extract_high_v8i16 V128:$Rn),
                  (extract_high_v8i16 (AArch64duplane16 (v8i16 V128_lo:$Rm),
                                                      VectorIndexH:$idx))))]> {

    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v2i32_indexed : BaseSIMDIndexed<0, U, 0, 0b10, opc,
                                      V128, V64,
                                      V128, VectorIndexS,
                                      asm, ".2d", ".2d", ".2s", ".s",
    [(set (v2i64 V128:$Rd),
        (OpNode (v2i32 V64:$Rn),
         (v2i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v4i32_indexed : BaseSIMDIndexed<1, U, 0, 0b10, opc,
                                      V128, V128,
                                      V128, VectorIndexS,
                                      asm#"2", ".2d", ".2d", ".4s", ".s",
    [(set (v2i64 V128:$Rd),
          (OpNode (extract_high_v4i32 V128:$Rn),
                  (extract_high_v4i32 (AArch64duplane32 (v4i32 V128:$Rm),
                                                      VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }
  }
}

multiclass SIMDVectorIndexedLongSDTied<bit U, bits<4> opc, string asm,
                                       SDPatternOperator OpNode> {
  let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {
  def v4i16_indexed : BaseSIMDIndexedTied<0, U, 0, 0b01, opc,
                                      V128, V64,
                                      V128_lo, VectorIndexH,
                                      asm, ".4s", ".4s", ".4h", ".h",
    [(set (v4i32 V128:$dst),
        (OpNode (v4i32 V128:$Rd), (v4i16 V64:$Rn),
         (v4i16 (AArch64duplane16 (v8i16 V128_lo:$Rm), VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v8i16_indexed : BaseSIMDIndexedTied<1, U, 0, 0b01, opc,
                                      V128, V128,
                                      V128_lo, VectorIndexH,
                                      asm#"2", ".4s", ".4s", ".8h", ".h",
    [(set (v4i32 V128:$dst),
          (OpNode (v4i32 V128:$Rd),
                  (extract_high_v8i16 V128:$Rn),
                  (extract_high_v8i16 (AArch64duplane16 (v8i16 V128_lo:$Rm),
                                                      VectorIndexH:$idx))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v2i32_indexed : BaseSIMDIndexedTied<0, U, 0, 0b10, opc,
                                      V128, V64,
                                      V128, VectorIndexS,
                                      asm, ".2d", ".2d", ".2s", ".s",
    [(set (v2i64 V128:$dst),
        (OpNode (v2i64 V128:$Rd), (v2i32 V64:$Rn),
         (v2i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  def v4i32_indexed : BaseSIMDIndexedTied<1, U, 0, 0b10, opc,
                                      V128, V128,
                                      V128, VectorIndexS,
                                      asm#"2", ".2d", ".2d", ".4s", ".s",
    [(set (v2i64 V128:$dst),
          (OpNode (v2i64 V128:$Rd),
                  (extract_high_v4i32 V128:$Rn),
                  (extract_high_v4i32 (AArch64duplane32 (v4i32 V128:$Rm),
                                                      VectorIndexS:$idx))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }
  }
}

//----------------------------------------------------------------------------
// AdvSIMD scalar shift by immediate
//----------------------------------------------------------------------------

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
class BaseSIMDScalarShift<bit U, bits<5> opc, bits<7> fixed_imm,
                     RegisterClass regtype1, RegisterClass regtype2,
                     Operand immtype, string asm, list<dag> pattern>
  : I<(outs regtype1:$Rd), (ins regtype2:$Rn, immtype:$imm),
      asm, "\t$Rd, $Rn, $imm", "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<7> imm;
  let Inst{31-30} = 0b01;
  let Inst{29}    = U;
  let Inst{28-23} = 0b111110;
  let Inst{22-16} = fixed_imm;
  let Inst{15-11} = opc;
  let Inst{10}    = 1;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rd;
}

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
class BaseSIMDScalarShiftTied<bit U, bits<5> opc, bits<7> fixed_imm,
                     RegisterClass regtype1, RegisterClass regtype2,
                     Operand immtype, string asm, list<dag> pattern>
  : I<(outs regtype1:$dst), (ins regtype1:$Rd, regtype2:$Rn, immtype:$imm),
      asm, "\t$Rd, $Rn, $imm", "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<7> imm;
  let Inst{31-30} = 0b01;
  let Inst{29}    = U;
  let Inst{28-23} = 0b111110;
  let Inst{22-16} = fixed_imm;
  let Inst{15-11} = opc;
  let Inst{10}    = 1;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rd;
}


multiclass SIMDFPScalarRShift<bit U, bits<5> opc, string asm> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def h : BaseSIMDScalarShift<U, opc, {0,0,1,?,?,?,?},
                              FPR16, FPR16, vecshiftR16, asm, []> {
    let Inst{19-16} = imm{3-0};
  }
  } // Predicates = [HasNEON, HasFullFP16]
  def s : BaseSIMDScalarShift<U, opc, {0,1,?,?,?,?,?},
                              FPR32, FPR32, vecshiftR32, asm, []> {
    let Inst{20-16} = imm{4-0};
  }
  def d : BaseSIMDScalarShift<U, opc, {1,?,?,?,?,?,?},
                              FPR64, FPR64, vecshiftR64, asm, []> {
    let Inst{21-16} = imm{5-0};
  }
}

multiclass SIMDScalarRShiftD<bit U, bits<5> opc, string asm,
                             SDPatternOperator OpNode> {
  def d : BaseSIMDScalarShift<U, opc, {1,?,?,?,?,?,?},
                              FPR64, FPR64, vecshiftR64, asm,
  [(set (i64 FPR64:$Rd),
     (OpNode (i64 FPR64:$Rn), (i32 vecshiftR64:$imm)))]> {
    let Inst{21-16} = imm{5-0};
  }

  def : Pat<(v1i64 (OpNode (v1i64 FPR64:$Rn), (i32 vecshiftR64:$imm))),
            (!cast<Instruction>(NAME # "d") FPR64:$Rn, vecshiftR64:$imm)>;
}

multiclass SIMDScalarRShiftDTied<bit U, bits<5> opc, string asm,
                                 SDPatternOperator OpNode = null_frag> {
  def d : BaseSIMDScalarShiftTied<U, opc, {1,?,?,?,?,?,?},
                              FPR64, FPR64, vecshiftR64, asm,
  [(set (i64 FPR64:$dst), (OpNode (i64 FPR64:$Rd), (i64 FPR64:$Rn),
                                                   (i32 vecshiftR64:$imm)))]> {
    let Inst{21-16} = imm{5-0};
  }

  def : Pat<(v1i64 (OpNode (v1i64 FPR64:$Rd), (v1i64 FPR64:$Rn),
                           (i32 vecshiftR64:$imm))),
            (!cast<Instruction>(NAME # "d") FPR64:$Rd, FPR64:$Rn,
                                            vecshiftR64:$imm)>;
}

multiclass SIMDScalarLShiftD<bit U, bits<5> opc, string asm,
                             SDPatternOperator OpNode> {
  def d : BaseSIMDScalarShift<U, opc, {1,?,?,?,?,?,?},
                              FPR64, FPR64, vecshiftL64, asm,
    [(set (v1i64 FPR64:$Rd),
       (OpNode (v1i64 FPR64:$Rn), (i32 vecshiftL64:$imm)))]> {
    let Inst{21-16} = imm{5-0};
  }
}

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
multiclass SIMDScalarLShiftDTied<bit U, bits<5> opc, string asm> {
  def d : BaseSIMDScalarShiftTied<U, opc, {1,?,?,?,?,?,?},
                              FPR64, FPR64, vecshiftL64, asm, []> {
    let Inst{21-16} = imm{5-0};
  }
}

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
multiclass SIMDScalarRShiftBHS<bit U, bits<5> opc, string asm,
                               SDPatternOperator OpNode = null_frag> {
  def b : BaseSIMDScalarShift<U, opc, {0,0,0,1,?,?,?},
                              FPR8, FPR16, vecshiftR8, asm, []> {
    let Inst{18-16} = imm{2-0};
  }

  def h : BaseSIMDScalarShift<U, opc, {0,0,1,?,?,?,?},
                              FPR16, FPR32, vecshiftR16, asm, []> {
    let Inst{19-16} = imm{3-0};
  }

  def s : BaseSIMDScalarShift<U, opc, {0,1,?,?,?,?,?},
                              FPR32, FPR64, vecshiftR32, asm,
    [(set (i32 FPR32:$Rd), (OpNode (i64 FPR64:$Rn), vecshiftR32:$imm))]> {
    let Inst{20-16} = imm{4-0};
  }
}

multiclass SIMDScalarLShiftBHSD<bit U, bits<5> opc, string asm,
                                SDPatternOperator OpNode> {
  def b : BaseSIMDScalarShift<U, opc, {0,0,0,1,?,?,?},
                              FPR8, FPR8, vecshiftL8, asm, []> {
    let Inst{18-16} = imm{2-0};
  }

  def h : BaseSIMDScalarShift<U, opc, {0,0,1,?,?,?,?},
                              FPR16, FPR16, vecshiftL16, asm, []> {
    let Inst{19-16} = imm{3-0};
  }

  def s : BaseSIMDScalarShift<U, opc, {0,1,?,?,?,?,?},
                              FPR32, FPR32, vecshiftL32, asm,
    [(set (i32 FPR32:$Rd), (OpNode (i32 FPR32:$Rn), (i32 vecshiftL32:$imm)))]> {
    let Inst{20-16} = imm{4-0};
  }

  def d : BaseSIMDScalarShift<U, opc, {1,?,?,?,?,?,?},
                              FPR64, FPR64, vecshiftL64, asm,
    [(set (i64 FPR64:$Rd), (OpNode (i64 FPR64:$Rn), (i32 vecshiftL64:$imm)))]> {
    let Inst{21-16} = imm{5-0};
  }

  def : Pat<(v1i64 (OpNode (v1i64 FPR64:$Rn), (i32 vecshiftL64:$imm))),
            (!cast<Instruction>(NAME # "d") FPR64:$Rn, vecshiftL64:$imm)>;
}

multiclass SIMDScalarRShiftBHSD<bit U, bits<5> opc, string asm> {
  def b : BaseSIMDScalarShift<U, opc, {0,0,0,1,?,?,?},
                              FPR8, FPR8, vecshiftR8, asm, []> {
    let Inst{18-16} = imm{2-0};
  }

  def h : BaseSIMDScalarShift<U, opc, {0,0,1,?,?,?,?},
                              FPR16, FPR16, vecshiftR16, asm, []> {
    let Inst{19-16} = imm{3-0};
  }

  def s : BaseSIMDScalarShift<U, opc, {0,1,?,?,?,?,?},
                              FPR32, FPR32, vecshiftR32, asm, []> {
    let Inst{20-16} = imm{4-0};
  }

  def d : BaseSIMDScalarShift<U, opc, {1,?,?,?,?,?,?},
                              FPR64, FPR64, vecshiftR64, asm, []> {
    let Inst{21-16} = imm{5-0};
  }
}

//----------------------------------------------------------------------------
// AdvSIMD vector x indexed element
//----------------------------------------------------------------------------

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
class BaseSIMDVectorShift<bit Q, bit U, bits<5> opc, bits<7> fixed_imm,
                     RegisterOperand dst_reg, RegisterOperand src_reg,
                     Operand immtype,
                     string asm, string dst_kind, string src_kind,
                     list<dag> pattern>
  : I<(outs dst_reg:$Rd), (ins src_reg:$Rn, immtype:$imm),
      asm, "{\t$Rd" # dst_kind # ", $Rn" # src_kind # ", $imm" #
           "|" # dst_kind # "\t$Rd, $Rn, $imm}", "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-23} = 0b011110;
  let Inst{22-16} = fixed_imm;
  let Inst{15-11} = opc;
  let Inst{10}    = 1;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

let mayStore = 0, mayLoad = 0, hasSideEffects = 0 in
class BaseSIMDVectorShiftTied<bit Q, bit U, bits<5> opc, bits<7> fixed_imm,
                     RegisterOperand vectype1, RegisterOperand vectype2,
                     Operand immtype,
                     string asm, string dst_kind, string src_kind,
                     list<dag> pattern>
  : I<(outs vectype1:$dst), (ins vectype1:$Rd, vectype2:$Rn, immtype:$imm),
      asm, "{\t$Rd" # dst_kind # ", $Rn" # src_kind # ", $imm" #
           "|" # dst_kind # "\t$Rd, $Rn, $imm}", "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-23} = 0b011110;
  let Inst{22-16} = fixed_imm;
  let Inst{15-11} = opc;
  let Inst{10}    = 1;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SIMDVectorRShiftSD<bit U, bits<5> opc, string asm,
                              Intrinsic OpNode> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4i16_shift : BaseSIMDVectorShift<0, U, opc, {0,0,1,?,?,?,?},
                                  V64, V64, vecshiftR16,
                                  asm, ".4h", ".4h",
      [(set (v4i16 V64:$Rd), (OpNode (v4f16 V64:$Rn), (i32 imm:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v8i16_shift : BaseSIMDVectorShift<1, U, opc, {0,0,1,?,?,?,?},
                                  V128, V128, vecshiftR16,
                                  asm, ".8h", ".8h",
      [(set (v8i16 V128:$Rd), (OpNode (v8f16 V128:$Rn), (i32 imm:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }
  } // Predicates = [HasNEON, HasFullFP16]
  def v2i32_shift : BaseSIMDVectorShift<0, U, opc, {0,1,?,?,?,?,?},
                                  V64, V64, vecshiftR32,
                                  asm, ".2s", ".2s",
      [(set (v2i32 V64:$Rd), (OpNode (v2f32 V64:$Rn), (i32 imm:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v4i32_shift : BaseSIMDVectorShift<1, U, opc, {0,1,?,?,?,?,?},
                                  V128, V128, vecshiftR32,
                                  asm, ".4s", ".4s",
      [(set (v4i32 V128:$Rd), (OpNode (v4f32 V128:$Rn), (i32 imm:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v2i64_shift : BaseSIMDVectorShift<1, U, opc, {1,?,?,?,?,?,?},
                                  V128, V128, vecshiftR64,
                                  asm, ".2d", ".2d",
      [(set (v2i64 V128:$Rd), (OpNode (v2f64 V128:$Rn), (i32 imm:$imm)))]> {
    bits<6> imm;
    let Inst{21-16} = imm;
  }
}

multiclass SIMDVectorRShiftToFP<bit U, bits<5> opc, string asm,
                                  Intrinsic OpNode> {
  let Predicates = [HasNEON, HasFullFP16] in {
  def v4i16_shift : BaseSIMDVectorShift<0, U, opc, {0,0,1,?,?,?,?},
                                  V64, V64, vecshiftR16,
                                  asm, ".4h", ".4h",
      [(set (v4f16 V64:$Rd), (OpNode (v4i16 V64:$Rn), (i32 imm:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v8i16_shift : BaseSIMDVectorShift<1, U, opc, {0,0,1,?,?,?,?},
                                  V128, V128, vecshiftR16,
                                  asm, ".8h", ".8h",
      [(set (v8f16 V128:$Rd), (OpNode (v8i16 V128:$Rn), (i32 imm:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }
  } // Predicates = [HasNEON, HasFullFP16]

  def v2i32_shift : BaseSIMDVectorShift<0, U, opc, {0,1,?,?,?,?,?},
                                  V64, V64, vecshiftR32,
                                  asm, ".2s", ".2s",
      [(set (v2f32 V64:$Rd), (OpNode (v2i32 V64:$Rn), (i32 imm:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v4i32_shift : BaseSIMDVectorShift<1, U, opc, {0,1,?,?,?,?,?},
                                  V128, V128, vecshiftR32,
                                  asm, ".4s", ".4s",
      [(set (v4f32 V128:$Rd), (OpNode (v4i32 V128:$Rn), (i32 imm:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v2i64_shift : BaseSIMDVectorShift<1, U, opc, {1,?,?,?,?,?,?},
                                  V128, V128, vecshiftR64,
                                  asm, ".2d", ".2d",
      [(set (v2f64 V128:$Rd), (OpNode (v2i64 V128:$Rn), (i32 imm:$imm)))]> {
    bits<6> imm;
    let Inst{21-16} = imm;
  }
}

multiclass SIMDVectorRShiftNarrowBHS<bit U, bits<5> opc, string asm,
                                     SDPatternOperator OpNode> {
  def v8i8_shift : BaseSIMDVectorShift<0, U, opc, {0,0,0,1,?,?,?},
                                  V64, V128, vecshiftR16Narrow,
                                  asm, ".8b", ".8h",
      [(set (v8i8 V64:$Rd), (OpNode (v8i16 V128:$Rn), vecshiftR16Narrow:$imm))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v16i8_shift : BaseSIMDVectorShiftTied<1, U, opc, {0,0,0,1,?,?,?},
                                  V128, V128, vecshiftR16Narrow,
                                  asm#"2", ".16b", ".8h", []> {
    bits<3> imm;
    let Inst{18-16} = imm;
    let hasSideEffects = 0;
  }

  def v4i16_shift : BaseSIMDVectorShift<0, U, opc, {0,0,1,?,?,?,?},
                                  V64, V128, vecshiftR32Narrow,
                                  asm, ".4h", ".4s",
      [(set (v4i16 V64:$Rd), (OpNode (v4i32 V128:$Rn), vecshiftR32Narrow:$imm))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v8i16_shift : BaseSIMDVectorShiftTied<1, U, opc, {0,0,1,?,?,?,?},
                                  V128, V128, vecshiftR32Narrow,
                                  asm#"2", ".8h", ".4s", []> {
    bits<4> imm;
    let Inst{19-16} = imm;
    let hasSideEffects = 0;
  }

  def v2i32_shift : BaseSIMDVectorShift<0, U, opc, {0,1,?,?,?,?,?},
                                  V64, V128, vecshiftR64Narrow,
                                  asm, ".2s", ".2d",
      [(set (v2i32 V64:$Rd), (OpNode (v2i64 V128:$Rn), vecshiftR64Narrow:$imm))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v4i32_shift : BaseSIMDVectorShiftTied<1, U, opc, {0,1,?,?,?,?,?},
                                  V128, V128, vecshiftR64Narrow,
                                  asm#"2", ".4s", ".2d", []> {
    bits<5> imm;
    let Inst{20-16} = imm;
    let hasSideEffects = 0;
  }

  // TableGen doesn't like patters w/ INSERT_SUBREG on the instructions
  // themselves, so put them here instead.

  // Patterns involving what's effectively an insert high and a normal
  // intrinsic, represented by CONCAT_VECTORS.
  def : Pat<(concat_vectors (v8i8 V64:$Rd),(OpNode (v8i16 V128:$Rn),
                                                   vecshiftR16Narrow:$imm)),
            (!cast<Instruction>(NAME # "v16i8_shift")
                (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                V128:$Rn, vecshiftR16Narrow:$imm)>;
  def : Pat<(concat_vectors (v4i16 V64:$Rd), (OpNode (v4i32 V128:$Rn),
                                                     vecshiftR32Narrow:$imm)),
            (!cast<Instruction>(NAME # "v8i16_shift")
                (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                V128:$Rn, vecshiftR32Narrow:$imm)>;
  def : Pat<(concat_vectors (v2i32 V64:$Rd), (OpNode (v2i64 V128:$Rn),
                                                     vecshiftR64Narrow:$imm)),
            (!cast<Instruction>(NAME # "v4i32_shift")
                (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                V128:$Rn, vecshiftR64Narrow:$imm)>;
}

multiclass SIMDVectorLShiftBHSD<bit U, bits<5> opc, string asm,
                                SDPatternOperator OpNode> {
  def v8i8_shift : BaseSIMDVectorShift<0, U, opc, {0,0,0,1,?,?,?},
                                  V64, V64, vecshiftL8,
                                  asm, ".8b", ".8b",
                 [(set (v8i8 V64:$Rd), (OpNode (v8i8 V64:$Rn),
                       (i32 vecshiftL8:$imm)))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v16i8_shift : BaseSIMDVectorShift<1, U, opc, {0,0,0,1,?,?,?},
                                  V128, V128, vecshiftL8,
                                  asm, ".16b", ".16b",
             [(set (v16i8 V128:$Rd), (OpNode (v16i8 V128:$Rn),
                   (i32 vecshiftL8:$imm)))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v4i16_shift : BaseSIMDVectorShift<0, U, opc, {0,0,1,?,?,?,?},
                                  V64, V64, vecshiftL16,
                                  asm, ".4h", ".4h",
              [(set (v4i16 V64:$Rd), (OpNode (v4i16 V64:$Rn),
                    (i32 vecshiftL16:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v8i16_shift : BaseSIMDVectorShift<1, U, opc, {0,0,1,?,?,?,?},
                                  V128, V128, vecshiftL16,
                                  asm, ".8h", ".8h",
            [(set (v8i16 V128:$Rd), (OpNode (v8i16 V128:$Rn),
                  (i32 vecshiftL16:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v2i32_shift : BaseSIMDVectorShift<0, U, opc, {0,1,?,?,?,?,?},
                                  V64, V64, vecshiftL32,
                                  asm, ".2s", ".2s",
              [(set (v2i32 V64:$Rd), (OpNode (v2i32 V64:$Rn),
                    (i32 vecshiftL32:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v4i32_shift : BaseSIMDVectorShift<1, U, opc, {0,1,?,?,?,?,?},
                                  V128, V128, vecshiftL32,
                                  asm, ".4s", ".4s",
            [(set (v4i32 V128:$Rd), (OpNode (v4i32 V128:$Rn),
                  (i32 vecshiftL32:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v2i64_shift : BaseSIMDVectorShift<1, U, opc, {1,?,?,?,?,?,?},
                                  V128, V128, vecshiftL64,
                                  asm, ".2d", ".2d",
            [(set (v2i64 V128:$Rd), (OpNode (v2i64 V128:$Rn),
                  (i32 vecshiftL64:$imm)))]> {
    bits<6> imm;
    let Inst{21-16} = imm;
  }
}

multiclass SIMDVectorRShiftBHSD<bit U, bits<5> opc, string asm,
                                SDPatternOperator OpNode> {
  def v8i8_shift : BaseSIMDVectorShift<0, U, opc, {0,0,0,1,?,?,?},
                                  V64, V64, vecshiftR8,
                                  asm, ".8b", ".8b",
                 [(set (v8i8 V64:$Rd), (OpNode (v8i8 V64:$Rn),
                       (i32 vecshiftR8:$imm)))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v16i8_shift : BaseSIMDVectorShift<1, U, opc, {0,0,0,1,?,?,?},
                                  V128, V128, vecshiftR8,
                                  asm, ".16b", ".16b",
             [(set (v16i8 V128:$Rd), (OpNode (v16i8 V128:$Rn),
                   (i32 vecshiftR8:$imm)))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v4i16_shift : BaseSIMDVectorShift<0, U, opc, {0,0,1,?,?,?,?},
                                  V64, V64, vecshiftR16,
                                  asm, ".4h", ".4h",
              [(set (v4i16 V64:$Rd), (OpNode (v4i16 V64:$Rn),
                    (i32 vecshiftR16:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v8i16_shift : BaseSIMDVectorShift<1, U, opc, {0,0,1,?,?,?,?},
                                  V128, V128, vecshiftR16,
                                  asm, ".8h", ".8h",
            [(set (v8i16 V128:$Rd), (OpNode (v8i16 V128:$Rn),
                  (i32 vecshiftR16:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v2i32_shift : BaseSIMDVectorShift<0, U, opc, {0,1,?,?,?,?,?},
                                  V64, V64, vecshiftR32,
                                  asm, ".2s", ".2s",
              [(set (v2i32 V64:$Rd), (OpNode (v2i32 V64:$Rn),
                    (i32 vecshiftR32:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v4i32_shift : BaseSIMDVectorShift<1, U, opc, {0,1,?,?,?,?,?},
                                  V128, V128, vecshiftR32,
                                  asm, ".4s", ".4s",
            [(set (v4i32 V128:$Rd), (OpNode (v4i32 V128:$Rn),
                  (i32 vecshiftR32:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v2i64_shift : BaseSIMDVectorShift<1, U, opc, {1,?,?,?,?,?,?},
                                  V128, V128, vecshiftR64,
                                  asm, ".2d", ".2d",
            [(set (v2i64 V128:$Rd), (OpNode (v2i64 V128:$Rn),
                  (i32 vecshiftR64:$imm)))]> {
    bits<6> imm;
    let Inst{21-16} = imm;
  }
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
multiclass SIMDVectorRShiftBHSDTied<bit U, bits<5> opc, string asm,
                                    SDPatternOperator OpNode = null_frag> {
  def v8i8_shift : BaseSIMDVectorShiftTied<0, U, opc, {0,0,0,1,?,?,?},
                                  V64, V64, vecshiftR8, asm, ".8b", ".8b",
                 [(set (v8i8 V64:$dst),
                   (OpNode (v8i8 V64:$Rd), (v8i8 V64:$Rn),
                           (i32 vecshiftR8:$imm)))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v16i8_shift : BaseSIMDVectorShiftTied<1, U, opc, {0,0,0,1,?,?,?},
                                  V128, V128, vecshiftR8, asm, ".16b", ".16b",
             [(set (v16i8 V128:$dst),
               (OpNode (v16i8 V128:$Rd), (v16i8 V128:$Rn),
                       (i32 vecshiftR8:$imm)))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v4i16_shift : BaseSIMDVectorShiftTied<0, U, opc, {0,0,1,?,?,?,?},
                                  V64, V64, vecshiftR16, asm, ".4h", ".4h",
              [(set (v4i16 V64:$dst),
                (OpNode (v4i16 V64:$Rd), (v4i16 V64:$Rn),
                        (i32 vecshiftR16:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v8i16_shift : BaseSIMDVectorShiftTied<1, U, opc, {0,0,1,?,?,?,?},
                                  V128, V128, vecshiftR16, asm, ".8h", ".8h",
            [(set (v8i16 V128:$dst),
              (OpNode (v8i16 V128:$Rd), (v8i16 V128:$Rn),
                      (i32 vecshiftR16:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v2i32_shift : BaseSIMDVectorShiftTied<0, U, opc, {0,1,?,?,?,?,?},
                                  V64, V64, vecshiftR32, asm, ".2s", ".2s",
              [(set (v2i32 V64:$dst),
                (OpNode (v2i32 V64:$Rd), (v2i32 V64:$Rn),
                        (i32 vecshiftR32:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v4i32_shift : BaseSIMDVectorShiftTied<1, U, opc, {0,1,?,?,?,?,?},
                                  V128, V128, vecshiftR32, asm, ".4s", ".4s",
            [(set (v4i32 V128:$dst),
              (OpNode (v4i32 V128:$Rd), (v4i32 V128:$Rn),
                      (i32 vecshiftR32:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v2i64_shift : BaseSIMDVectorShiftTied<1, U, opc, {1,?,?,?,?,?,?},
                                  V128, V128, vecshiftR64,
                                  asm, ".2d", ".2d", [(set (v2i64 V128:$dst),
              (OpNode (v2i64 V128:$Rd), (v2i64 V128:$Rn),
                      (i32 vecshiftR64:$imm)))]> {
    bits<6> imm;
    let Inst{21-16} = imm;
  }
}

multiclass SIMDVectorLShiftBHSDTied<bit U, bits<5> opc, string asm,
                                    SDPatternOperator OpNode = null_frag> {
  def v8i8_shift : BaseSIMDVectorShiftTied<0, U, opc, {0,0,0,1,?,?,?},
                                  V64, V64, vecshiftL8,
                                  asm, ".8b", ".8b",
                    [(set (v8i8 V64:$dst),
                          (OpNode (v8i8 V64:$Rd), (v8i8 V64:$Rn),
                                  (i32 vecshiftL8:$imm)))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v16i8_shift : BaseSIMDVectorShiftTied<1, U, opc, {0,0,0,1,?,?,?},
                                  V128, V128, vecshiftL8,
                                  asm, ".16b", ".16b",
                    [(set (v16i8 V128:$dst),
                          (OpNode (v16i8 V128:$Rd), (v16i8 V128:$Rn),
                                  (i32 vecshiftL8:$imm)))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v4i16_shift : BaseSIMDVectorShiftTied<0, U, opc, {0,0,1,?,?,?,?},
                                  V64, V64, vecshiftL16,
                                  asm, ".4h", ".4h",
                    [(set (v4i16 V64:$dst),
                           (OpNode (v4i16 V64:$Rd), (v4i16 V64:$Rn),
                                   (i32 vecshiftL16:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v8i16_shift : BaseSIMDVectorShiftTied<1, U, opc, {0,0,1,?,?,?,?},
                                  V128, V128, vecshiftL16,
                                  asm, ".8h", ".8h",
                    [(set (v8i16 V128:$dst),
                          (OpNode (v8i16 V128:$Rd), (v8i16 V128:$Rn),
                                  (i32 vecshiftL16:$imm)))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v2i32_shift : BaseSIMDVectorShiftTied<0, U, opc, {0,1,?,?,?,?,?},
                                  V64, V64, vecshiftL32,
                                  asm, ".2s", ".2s",
                    [(set (v2i32 V64:$dst),
                          (OpNode (v2i32 V64:$Rd), (v2i32 V64:$Rn),
                                  (i32 vecshiftL32:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v4i32_shift : BaseSIMDVectorShiftTied<1, U, opc, {0,1,?,?,?,?,?},
                                  V128, V128, vecshiftL32,
                                  asm, ".4s", ".4s",
                    [(set (v4i32 V128:$dst),
                          (OpNode (v4i32 V128:$Rd), (v4i32 V128:$Rn),
                                  (i32 vecshiftL32:$imm)))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v2i64_shift : BaseSIMDVectorShiftTied<1, U, opc, {1,?,?,?,?,?,?},
                                  V128, V128, vecshiftL64,
                                  asm, ".2d", ".2d",
                    [(set (v2i64 V128:$dst),
                          (OpNode (v2i64 V128:$Rd), (v2i64 V128:$Rn),
                                  (i32 vecshiftL64:$imm)))]> {
    bits<6> imm;
    let Inst{21-16} = imm;
  }
}

multiclass SIMDVectorLShiftLongBHSD<bit U, bits<5> opc, string asm,
                                   SDPatternOperator OpNode> {
  def v8i8_shift : BaseSIMDVectorShift<0, U, opc, {0,0,0,1,?,?,?},
                                  V128, V64, vecshiftL8, asm, ".8h", ".8b",
      [(set (v8i16 V128:$Rd), (OpNode (v8i8 V64:$Rn), vecshiftL8:$imm))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v16i8_shift : BaseSIMDVectorShift<1, U, opc, {0,0,0,1,?,?,?},
                                  V128, V128, vecshiftL8,
                                  asm#"2", ".8h", ".16b",
      [(set (v8i16 V128:$Rd),
            (OpNode (extract_high_v16i8 V128:$Rn), vecshiftL8:$imm))]> {
    bits<3> imm;
    let Inst{18-16} = imm;
  }

  def v4i16_shift : BaseSIMDVectorShift<0, U, opc, {0,0,1,?,?,?,?},
                                  V128, V64, vecshiftL16, asm, ".4s", ".4h",
      [(set (v4i32 V128:$Rd), (OpNode (v4i16 V64:$Rn), vecshiftL16:$imm))]> {
    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v8i16_shift : BaseSIMDVectorShift<1, U, opc, {0,0,1,?,?,?,?},
                                  V128, V128, vecshiftL16,
                                  asm#"2", ".4s", ".8h",
      [(set (v4i32 V128:$Rd),
            (OpNode (extract_high_v8i16 V128:$Rn), vecshiftL16:$imm))]> {

    bits<4> imm;
    let Inst{19-16} = imm;
  }

  def v2i32_shift : BaseSIMDVectorShift<0, U, opc, {0,1,?,?,?,?,?},
                                  V128, V64, vecshiftL32, asm, ".2d", ".2s",
      [(set (v2i64 V128:$Rd), (OpNode (v2i32 V64:$Rn), vecshiftL32:$imm))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }

  def v4i32_shift : BaseSIMDVectorShift<1, U, opc, {0,1,?,?,?,?,?},
                                  V128, V128, vecshiftL32,
                                  asm#"2", ".2d", ".4s",
      [(set (v2i64 V128:$Rd),
            (OpNode (extract_high_v4i32 V128:$Rn), vecshiftL32:$imm))]> {
    bits<5> imm;
    let Inst{20-16} = imm;
  }
}


//---
// Vector load/store
//---
// SIMD ldX/stX no-index memory references don't allow the optional
// ", #0" constant and handle post-indexing explicitly, so we use
// a more specialized parse method for them. Otherwise, it's the same as
// the general GPR64sp handling.

class BaseSIMDLdSt<bit Q, bit L, bits<4> opcode, bits<2> size,
                   string asm, dag oops, dag iops, list<dag> pattern>
  : I<oops, iops, asm, "\t$Vt, [$Rn]", "", pattern> {
  bits<5> Vt;
  bits<5> Rn;
  let Inst{31} = 0;
  let Inst{30} = Q;
  let Inst{29-23} = 0b0011000;
  let Inst{22} = L;
  let Inst{21-16} = 0b000000;
  let Inst{15-12} = opcode;
  let Inst{11-10} = size;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Vt;
}

class BaseSIMDLdStPost<bit Q, bit L, bits<4> opcode, bits<2> size,
                       string asm, dag oops, dag iops>
  : I<oops, iops, asm, "\t$Vt, [$Rn], $Xm", "$Rn = $wback", []> {
  bits<5> Vt;
  bits<5> Rn;
  bits<5> Xm;
  let Inst{31} = 0;
  let Inst{30} = Q;
  let Inst{29-23} = 0b0011001;
  let Inst{22} = L;
  let Inst{21} = 0;
  let Inst{20-16} = Xm;
  let Inst{15-12} = opcode;
  let Inst{11-10} = size;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Vt;
}

// The immediate form of AdvSIMD post-indexed addressing is encoded with
// register post-index addressing from the zero register.
multiclass SIMDLdStAliases<string BaseName, string asm, string layout, string Count,
                           int Offset, int Size> {
  // E.g. "ld1 { v0.8b, v1.8b }, [x1], #16"
  //      "ld1\t$Vt, [$Rn], #16"
  // may get mapped to
  //      (LD1Twov8b_POST VecListTwo8b:$Vt, GPR64sp:$Rn, XZR)
  def : InstAlias<asm # "\t$Vt, [$Rn], #" # Offset,
                  (!cast<Instruction>(BaseName # Count # "v" # layout # "_POST")
                      GPR64sp:$Rn,
                      !cast<RegisterOperand>("VecList" # Count # layout):$Vt,
                      XZR), 1>;

  // E.g. "ld1.8b { v0, v1 }, [x1], #16"
  //      "ld1.8b\t$Vt, [$Rn], #16"
  // may get mapped to
  //      (LD1Twov8b_POST VecListTwo64:$Vt, GPR64sp:$Rn, XZR)
  def : InstAlias<asm # "." # layout # "\t$Vt, [$Rn], #" # Offset,
                  (!cast<Instruction>(BaseName # Count # "v" # layout # "_POST")
                      GPR64sp:$Rn,
                      !cast<RegisterOperand>("VecList" # Count # Size):$Vt,
                      XZR), 0>;

  // E.g. "ld1.8b { v0, v1 }, [x1]"
  //      "ld1\t$Vt, [$Rn]"
  // may get mapped to
  //      (LD1Twov8b VecListTwo64:$Vt, GPR64sp:$Rn)
  def : InstAlias<asm # "." # layout # "\t$Vt, [$Rn]",
                  (!cast<Instruction>(BaseName # Count # "v" # layout)
                      !cast<RegisterOperand>("VecList" # Count # Size):$Vt,
                      GPR64sp:$Rn), 0>;

  // E.g. "ld1.8b { v0, v1 }, [x1], x2"
  //      "ld1\t$Vt, [$Rn], $Xm"
  // may get mapped to
  //      (LD1Twov8b_POST VecListTwo64:$Vt, GPR64sp:$Rn, GPR64pi8:$Xm)
  def : InstAlias<asm # "." # layout # "\t$Vt, [$Rn], $Xm",
                  (!cast<Instruction>(BaseName # Count # "v" # layout # "_POST")
                      GPR64sp:$Rn,
                      !cast<RegisterOperand>("VecList" # Count # Size):$Vt,
                      !cast<RegisterOperand>("GPR64pi" # Offset):$Xm), 0>;
}

multiclass BaseSIMDLdN<string BaseName, string Count, string asm, string veclist,
                       int Offset128, int Offset64, bits<4> opcode> {
  let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
    def v16b: BaseSIMDLdSt<1, 1, opcode, 0b00, asm,
                           (outs !cast<RegisterOperand>(veclist # "16b"):$Vt),
                           (ins GPR64sp:$Rn), []>;
    def v8h : BaseSIMDLdSt<1, 1, opcode, 0b01, asm,
                           (outs !cast<RegisterOperand>(veclist # "8h"):$Vt),
                           (ins GPR64sp:$Rn), []>;
    def v4s : BaseSIMDLdSt<1, 1, opcode, 0b10, asm,
                           (outs !cast<RegisterOperand>(veclist # "4s"):$Vt),
                           (ins GPR64sp:$Rn), []>;
    def v2d : BaseSIMDLdSt<1, 1, opcode, 0b11, asm,
                           (outs !cast<RegisterOperand>(veclist # "2d"):$Vt),
                           (ins GPR64sp:$Rn), []>;
    def v8b : BaseSIMDLdSt<0, 1, opcode, 0b00, asm,
                           (outs !cast<RegisterOperand>(veclist # "8b"):$Vt),
                           (ins GPR64sp:$Rn), []>;
    def v4h : BaseSIMDLdSt<0, 1, opcode, 0b01, asm,
                           (outs !cast<RegisterOperand>(veclist # "4h"):$Vt),
                           (ins GPR64sp:$Rn), []>;
    def v2s : BaseSIMDLdSt<0, 1, opcode, 0b10, asm,
                           (outs !cast<RegisterOperand>(veclist # "2s"):$Vt),
                           (ins GPR64sp:$Rn), []>;


    def v16b_POST: BaseSIMDLdStPost<1, 1, opcode, 0b00, asm,
                       (outs GPR64sp:$wback,
                             !cast<RegisterOperand>(veclist # "16b"):$Vt),
                       (ins GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset128):$Xm)>;
    def v8h_POST : BaseSIMDLdStPost<1, 1, opcode, 0b01, asm,
                       (outs GPR64sp:$wback,
                             !cast<RegisterOperand>(veclist # "8h"):$Vt),
                       (ins GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset128):$Xm)>;
    def v4s_POST : BaseSIMDLdStPost<1, 1, opcode, 0b10, asm,
                       (outs GPR64sp:$wback,
                             !cast<RegisterOperand>(veclist # "4s"):$Vt),
                       (ins GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset128):$Xm)>;
    def v2d_POST : BaseSIMDLdStPost<1, 1, opcode, 0b11, asm,
                       (outs GPR64sp:$wback,
                             !cast<RegisterOperand>(veclist # "2d"):$Vt),
                       (ins GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset128):$Xm)>;
    def v8b_POST : BaseSIMDLdStPost<0, 1, opcode, 0b00, asm,
                       (outs GPR64sp:$wback,
                             !cast<RegisterOperand>(veclist # "8b"):$Vt),
                       (ins GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset64):$Xm)>;
    def v4h_POST : BaseSIMDLdStPost<0, 1, opcode, 0b01, asm,
                       (outs GPR64sp:$wback,
                             !cast<RegisterOperand>(veclist # "4h"):$Vt),
                       (ins GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset64):$Xm)>;
    def v2s_POST : BaseSIMDLdStPost<0, 1, opcode, 0b10, asm,
                       (outs GPR64sp:$wback,
                             !cast<RegisterOperand>(veclist # "2s"):$Vt),
                       (ins GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset64):$Xm)>;
  }

  defm : SIMDLdStAliases<BaseName, asm, "16b", Count, Offset128, 128>;
  defm : SIMDLdStAliases<BaseName, asm, "8h", Count, Offset128, 128>;
  defm : SIMDLdStAliases<BaseName, asm, "4s", Count, Offset128, 128>;
  defm : SIMDLdStAliases<BaseName, asm, "2d", Count, Offset128, 128>;
  defm : SIMDLdStAliases<BaseName, asm, "8b", Count, Offset64, 64>;
  defm : SIMDLdStAliases<BaseName, asm, "4h", Count, Offset64, 64>;
  defm : SIMDLdStAliases<BaseName, asm, "2s", Count, Offset64, 64>;
}

// Only ld1/st1 has a v1d version.
multiclass BaseSIMDStN<string BaseName, string Count, string asm, string veclist,
                       int Offset128, int Offset64, bits<4> opcode> {
  let hasSideEffects = 0, mayStore = 1, mayLoad = 0 in {
    def v16b : BaseSIMDLdSt<1, 0, opcode, 0b00, asm, (outs),
                            (ins !cast<RegisterOperand>(veclist # "16b"):$Vt,
                                 GPR64sp:$Rn), []>;
    def v8h : BaseSIMDLdSt<1, 0, opcode, 0b01, asm, (outs),
                           (ins !cast<RegisterOperand>(veclist # "8h"):$Vt,
                                GPR64sp:$Rn), []>;
    def v4s : BaseSIMDLdSt<1, 0, opcode, 0b10, asm, (outs),
                           (ins !cast<RegisterOperand>(veclist # "4s"):$Vt,
                                GPR64sp:$Rn), []>;
    def v2d : BaseSIMDLdSt<1, 0, opcode, 0b11, asm, (outs),
                           (ins !cast<RegisterOperand>(veclist # "2d"):$Vt,
                                GPR64sp:$Rn), []>;
    def v8b : BaseSIMDLdSt<0, 0, opcode, 0b00, asm, (outs),
                           (ins !cast<RegisterOperand>(veclist # "8b"):$Vt,
                                GPR64sp:$Rn), []>;
    def v4h : BaseSIMDLdSt<0, 0, opcode, 0b01, asm, (outs),
                           (ins !cast<RegisterOperand>(veclist # "4h"):$Vt,
                                GPR64sp:$Rn), []>;
    def v2s : BaseSIMDLdSt<0, 0, opcode, 0b10, asm, (outs),
                           (ins !cast<RegisterOperand>(veclist # "2s"):$Vt,
                                GPR64sp:$Rn), []>;

    def v16b_POST : BaseSIMDLdStPost<1, 0, opcode, 0b00, asm,
                       (outs GPR64sp:$wback),
                       (ins !cast<RegisterOperand>(veclist # "16b"):$Vt,
                            GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset128):$Xm)>;
    def v8h_POST : BaseSIMDLdStPost<1, 0, opcode, 0b01, asm,
                       (outs GPR64sp:$wback),
                       (ins !cast<RegisterOperand>(veclist # "8h"):$Vt,
                            GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset128):$Xm)>;
    def v4s_POST : BaseSIMDLdStPost<1, 0, opcode, 0b10, asm,
                       (outs GPR64sp:$wback),
                       (ins !cast<RegisterOperand>(veclist # "4s"):$Vt,
                            GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset128):$Xm)>;
    def v2d_POST : BaseSIMDLdStPost<1, 0, opcode, 0b11, asm,
                       (outs GPR64sp:$wback),
                       (ins !cast<RegisterOperand>(veclist # "2d"):$Vt,
                            GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset128):$Xm)>;
    def v8b_POST : BaseSIMDLdStPost<0, 0, opcode, 0b00, asm,
                       (outs GPR64sp:$wback),
                       (ins !cast<RegisterOperand>(veclist # "8b"):$Vt,
                            GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset64):$Xm)>;
    def v4h_POST : BaseSIMDLdStPost<0, 0, opcode, 0b01, asm,
                       (outs GPR64sp:$wback),
                       (ins !cast<RegisterOperand>(veclist # "4h"):$Vt,
                            GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset64):$Xm)>;
    def v2s_POST : BaseSIMDLdStPost<0, 0, opcode, 0b10, asm,
                       (outs GPR64sp:$wback),
                       (ins !cast<RegisterOperand>(veclist # "2s"):$Vt,
                            GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset64):$Xm)>;
  }

  defm : SIMDLdStAliases<BaseName, asm, "16b", Count, Offset128, 128>;
  defm : SIMDLdStAliases<BaseName, asm, "8h", Count, Offset128, 128>;
  defm : SIMDLdStAliases<BaseName, asm, "4s", Count, Offset128, 128>;
  defm : SIMDLdStAliases<BaseName, asm, "2d", Count, Offset128, 128>;
  defm : SIMDLdStAliases<BaseName, asm, "8b", Count, Offset64, 64>;
  defm : SIMDLdStAliases<BaseName, asm, "4h", Count, Offset64, 64>;
  defm : SIMDLdStAliases<BaseName, asm, "2s", Count, Offset64, 64>;
}

multiclass BaseSIMDLd1<string BaseName, string Count, string asm, string veclist,
                       int Offset128, int Offset64, bits<4> opcode>
  : BaseSIMDLdN<BaseName, Count, asm, veclist, Offset128, Offset64, opcode> {

  // LD1 instructions have extra "1d" variants.
  let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
    def v1d : BaseSIMDLdSt<0, 1, opcode, 0b11, asm,
                           (outs !cast<RegisterOperand>(veclist # "1d"):$Vt),
                           (ins GPR64sp:$Rn), []>;

    def v1d_POST : BaseSIMDLdStPost<0, 1, opcode, 0b11, asm,
                       (outs GPR64sp:$wback,
                             !cast<RegisterOperand>(veclist # "1d"):$Vt),
                       (ins GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset64):$Xm)>;
  }

  defm : SIMDLdStAliases<BaseName, asm, "1d", Count, Offset64, 64>;
}

multiclass BaseSIMDSt1<string BaseName, string Count, string asm, string veclist,
                       int Offset128, int Offset64, bits<4> opcode>
  : BaseSIMDStN<BaseName, Count, asm, veclist, Offset128, Offset64, opcode> {

  // ST1 instructions have extra "1d" variants.
  let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in {
    def v1d : BaseSIMDLdSt<0, 0, opcode, 0b11, asm, (outs),
                           (ins !cast<RegisterOperand>(veclist # "1d"):$Vt,
                                GPR64sp:$Rn), []>;

    def v1d_POST : BaseSIMDLdStPost<0, 0, opcode, 0b11, asm,
                       (outs GPR64sp:$wback),
                       (ins !cast<RegisterOperand>(veclist # "1d"):$Vt,
                            GPR64sp:$Rn,
                            !cast<RegisterOperand>("GPR64pi" # Offset64):$Xm)>;
  }

  defm : SIMDLdStAliases<BaseName, asm, "1d", Count, Offset64, 64>;
}

multiclass SIMDLd1Multiple<string asm> {
  defm One   : BaseSIMDLd1<NAME, "One", asm, "VecListOne", 16, 8,  0b0111>;
  defm Two   : BaseSIMDLd1<NAME, "Two", asm, "VecListTwo", 32, 16, 0b1010>;
  defm Three : BaseSIMDLd1<NAME, "Three", asm, "VecListThree", 48, 24, 0b0110>;
  defm Four  : BaseSIMDLd1<NAME, "Four", asm, "VecListFour", 64, 32, 0b0010>;
}

multiclass SIMDSt1Multiple<string asm> {
  defm One   : BaseSIMDSt1<NAME, "One", asm, "VecListOne", 16, 8,  0b0111>;
  defm Two   : BaseSIMDSt1<NAME, "Two", asm, "VecListTwo", 32, 16, 0b1010>;
  defm Three : BaseSIMDSt1<NAME, "Three", asm, "VecListThree", 48, 24, 0b0110>;
  defm Four  : BaseSIMDSt1<NAME, "Four", asm, "VecListFour", 64, 32, 0b0010>;
}

multiclass SIMDLd2Multiple<string asm> {
  defm Two : BaseSIMDLdN<NAME, "Two", asm, "VecListTwo", 32, 16, 0b1000>;
}

multiclass SIMDSt2Multiple<string asm> {
  defm Two : BaseSIMDStN<NAME, "Two", asm, "VecListTwo", 32, 16, 0b1000>;
}

multiclass SIMDLd3Multiple<string asm> {
  defm Three : BaseSIMDLdN<NAME, "Three", asm, "VecListThree", 48, 24, 0b0100>;
}

multiclass SIMDSt3Multiple<string asm> {
  defm Three : BaseSIMDStN<NAME, "Three", asm, "VecListThree", 48, 24, 0b0100>;
}

multiclass SIMDLd4Multiple<string asm> {
  defm Four : BaseSIMDLdN<NAME, "Four", asm, "VecListFour", 64, 32, 0b0000>;
}

multiclass SIMDSt4Multiple<string asm> {
  defm Four : BaseSIMDStN<NAME, "Four", asm, "VecListFour", 64, 32, 0b0000>;
}

//---
// AdvSIMD Load/store single-element
//---

class BaseSIMDLdStSingle<bit L, bit R, bits<3> opcode,
                         string asm, string operands, string cst,
                         dag oops, dag iops, list<dag> pattern>
  : I<oops, iops, asm, operands, cst, pattern> {
  bits<5> Vt;
  bits<5> Rn;
  let Inst{31} = 0;
  let Inst{29-24} = 0b001101;
  let Inst{22} = L;
  let Inst{21} = R;
  let Inst{15-13} = opcode;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Vt;
}

class BaseSIMDLdStSingleTied<bit L, bit R, bits<3> opcode,
                         string asm, string operands, string cst,
                         dag oops, dag iops, list<dag> pattern>
  : I<oops, iops, asm, operands, "$Vt = $dst," # cst, pattern> {
  bits<5> Vt;
  bits<5> Rn;
  let Inst{31} = 0;
  let Inst{29-24} = 0b001101;
  let Inst{22} = L;
  let Inst{21} = R;
  let Inst{15-13} = opcode;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Vt;
}


let mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDLdR<bit Q, bit R, bits<3> opcode, bit S, bits<2> size, string asm,
                  DAGOperand listtype>
  : BaseSIMDLdStSingle<1, R, opcode, asm, "\t$Vt, [$Rn]", "",
                       (outs listtype:$Vt), (ins GPR64sp:$Rn),
                       []> {
  let Inst{30} = Q;
  let Inst{23} = 0;
  let Inst{20-16} = 0b00000;
  let Inst{12} = S;
  let Inst{11-10} = size;
}
let mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDLdRPost<bit Q, bit R, bits<3> opcode, bit S, bits<2> size,
                      string asm, DAGOperand listtype, DAGOperand GPR64pi>
  : BaseSIMDLdStSingle<1, R, opcode, asm, "\t$Vt, [$Rn], $Xm",
                       "$Rn = $wback",
                       (outs GPR64sp:$wback, listtype:$Vt),
                       (ins GPR64sp:$Rn, GPR64pi:$Xm), []> {
  bits<5> Xm;
  let Inst{30} = Q;
  let Inst{23} = 1;
  let Inst{20-16} = Xm;
  let Inst{12} = S;
  let Inst{11-10} = size;
}

multiclass SIMDLdrAliases<string BaseName, string asm, string layout, string Count,
                          int Offset, int Size> {
  // E.g. "ld1r { v0.8b }, [x1], #1"
  //      "ld1r.8b\t$Vt, [$Rn], #1"
  // may get mapped to
  //      (LD1Rv8b_POST VecListOne8b:$Vt, GPR64sp:$Rn, XZR)
  def : InstAlias<asm # "\t$Vt, [$Rn], #" # Offset,
                  (!cast<Instruction>(BaseName # "v" # layout # "_POST")
                      GPR64sp:$Rn,
                      !cast<RegisterOperand>("VecList" # Count # layout):$Vt,
                      XZR), 1>;

  // E.g. "ld1r.8b { v0 }, [x1], #1"
  //      "ld1r.8b\t$Vt, [$Rn], #1"
  // may get mapped to
  //      (LD1Rv8b_POST VecListOne64:$Vt, GPR64sp:$Rn, XZR)
  def : InstAlias<asm # "." # layout # "\t$Vt, [$Rn], #" # Offset,
                  (!cast<Instruction>(BaseName # "v" # layout # "_POST")
                      GPR64sp:$Rn,
                      !cast<RegisterOperand>("VecList" # Count # Size):$Vt,
                      XZR), 0>;

  // E.g. "ld1r.8b { v0 }, [x1]"
  //      "ld1r.8b\t$Vt, [$Rn]"
  // may get mapped to
  //      (LD1Rv8b VecListOne64:$Vt, GPR64sp:$Rn)
  def : InstAlias<asm # "." # layout # "\t$Vt, [$Rn]",
                  (!cast<Instruction>(BaseName # "v" # layout)
                      !cast<RegisterOperand>("VecList" # Count # Size):$Vt,
                      GPR64sp:$Rn), 0>;

  // E.g. "ld1r.8b { v0 }, [x1], x2"
  //      "ld1r.8b\t$Vt, [$Rn], $Xm"
  // may get mapped to
  //      (LD1Rv8b_POST VecListOne64:$Vt, GPR64sp:$Rn, GPR64pi1:$Xm)
  def : InstAlias<asm # "." # layout # "\t$Vt, [$Rn], $Xm",
                  (!cast<Instruction>(BaseName # "v" # layout # "_POST")
                      GPR64sp:$Rn,
                      !cast<RegisterOperand>("VecList" # Count # Size):$Vt,
                      !cast<RegisterOperand>("GPR64pi" # Offset):$Xm), 0>;
}

multiclass SIMDLdR<bit R, bits<3> opcode, bit S, string asm, string Count,
  int Offset1, int Offset2, int Offset4, int Offset8> {
  def v8b : BaseSIMDLdR<0, R, opcode, S, 0b00, asm,
                        !cast<DAGOperand>("VecList" # Count # "8b")>;
  def v16b: BaseSIMDLdR<1, R, opcode, S, 0b00, asm,
                        !cast<DAGOperand>("VecList" # Count #"16b")>;
  def v4h : BaseSIMDLdR<0, R, opcode, S, 0b01, asm,
                        !cast<DAGOperand>("VecList" # Count #"4h")>;
  def v8h : BaseSIMDLdR<1, R, opcode, S, 0b01, asm,
                        !cast<DAGOperand>("VecList" # Count #"8h")>;
  def v2s : BaseSIMDLdR<0, R, opcode, S, 0b10, asm,
                        !cast<DAGOperand>("VecList" # Count #"2s")>;
  def v4s : BaseSIMDLdR<1, R, opcode, S, 0b10, asm,
                        !cast<DAGOperand>("VecList" # Count #"4s")>;
  def v1d : BaseSIMDLdR<0, R, opcode, S, 0b11, asm,
                        !cast<DAGOperand>("VecList" # Count #"1d")>;
  def v2d : BaseSIMDLdR<1, R, opcode, S, 0b11, asm,
                        !cast<DAGOperand>("VecList" # Count #"2d")>;

  def v8b_POST : BaseSIMDLdRPost<0, R, opcode, S, 0b00, asm,
                                 !cast<DAGOperand>("VecList" # Count # "8b"),
                                 !cast<DAGOperand>("GPR64pi" # Offset1)>;
  def v16b_POST: BaseSIMDLdRPost<1, R, opcode, S, 0b00, asm,
                                 !cast<DAGOperand>("VecList" # Count # "16b"),
                                 !cast<DAGOperand>("GPR64pi" # Offset1)>;
  def v4h_POST : BaseSIMDLdRPost<0, R, opcode, S, 0b01, asm,
                                 !cast<DAGOperand>("VecList" # Count # "4h"),
                                 !cast<DAGOperand>("GPR64pi" # Offset2)>;
  def v8h_POST : BaseSIMDLdRPost<1, R, opcode, S, 0b01, asm,
                                 !cast<DAGOperand>("VecList" # Count # "8h"),
                                 !cast<DAGOperand>("GPR64pi" # Offset2)>;
  def v2s_POST : BaseSIMDLdRPost<0, R, opcode, S, 0b10, asm,
                                 !cast<DAGOperand>("VecList" # Count # "2s"),
                                 !cast<DAGOperand>("GPR64pi" # Offset4)>;
  def v4s_POST : BaseSIMDLdRPost<1, R, opcode, S, 0b10, asm,
                                 !cast<DAGOperand>("VecList" # Count # "4s"),
                                 !cast<DAGOperand>("GPR64pi" # Offset4)>;
  def v1d_POST : BaseSIMDLdRPost<0, R, opcode, S, 0b11, asm,
                                 !cast<DAGOperand>("VecList" # Count # "1d"),
                                 !cast<DAGOperand>("GPR64pi" # Offset8)>;
  def v2d_POST : BaseSIMDLdRPost<1, R, opcode, S, 0b11, asm,
                                 !cast<DAGOperand>("VecList" # Count # "2d"),
                                 !cast<DAGOperand>("GPR64pi" # Offset8)>;

  defm : SIMDLdrAliases<NAME, asm, "8b",  Count, Offset1,  64>;
  defm : SIMDLdrAliases<NAME, asm, "16b", Count, Offset1, 128>;
  defm : SIMDLdrAliases<NAME, asm, "4h",  Count, Offset2,  64>;
  defm : SIMDLdrAliases<NAME, asm, "8h",  Count, Offset2, 128>;
  defm : SIMDLdrAliases<NAME, asm, "2s",  Count, Offset4,  64>;
  defm : SIMDLdrAliases<NAME, asm, "4s",  Count, Offset4, 128>;
  defm : SIMDLdrAliases<NAME, asm, "1d",  Count, Offset8,  64>;
  defm : SIMDLdrAliases<NAME, asm, "2d",  Count, Offset8, 128>;
}

class SIMDLdStSingleB<bit L, bit R, bits<3> opcode, string asm,
                      dag oops, dag iops, list<dag> pattern>
  : BaseSIMDLdStSingle<L, R, opcode, asm, "\t$Vt$idx, [$Rn]", "", oops, iops,
                       pattern> {
  // idx encoded in Q:S:size fields.
  bits<4> idx;
  let Inst{30} = idx{3};
  let Inst{23} = 0;
  let Inst{20-16} = 0b00000;
  let Inst{12} = idx{2};
  let Inst{11-10} = idx{1-0};
}
class SIMDLdStSingleBTied<bit L, bit R, bits<3> opcode, string asm,
                      dag oops, dag iops, list<dag> pattern>
  : BaseSIMDLdStSingleTied<L, R, opcode, asm, "\t$Vt$idx, [$Rn]", "",
                           oops, iops, pattern> {
  // idx encoded in Q:S:size fields.
  bits<4> idx;
  let Inst{30} = idx{3};
  let Inst{23} = 0;
  let Inst{20-16} = 0b00000;
  let Inst{12} = idx{2};
  let Inst{11-10} = idx{1-0};
}
class SIMDLdStSingleBPost<bit L, bit R, bits<3> opcode, string asm,
                          dag oops, dag iops>
  : BaseSIMDLdStSingle<L, R, opcode, asm, "\t$Vt$idx, [$Rn], $Xm",
                       "$Rn = $wback", oops, iops, []> {
  // idx encoded in Q:S:size fields.
  bits<4> idx;
  bits<5> Xm;
  let Inst{30} = idx{3};
  let Inst{23} = 1;
  let Inst{20-16} = Xm;
  let Inst{12} = idx{2};
  let Inst{11-10} = idx{1-0};
}
class SIMDLdStSingleBTiedPost<bit L, bit R, bits<3> opcode, string asm,
                          dag oops, dag iops>
  : BaseSIMDLdStSingleTied<L, R, opcode, asm, "\t$Vt$idx, [$Rn], $Xm",
                           "$Rn = $wback", oops, iops, []> {
  // idx encoded in Q:S:size fields.
  bits<4> idx;
  bits<5> Xm;
  let Inst{30} = idx{3};
  let Inst{23} = 1;
  let Inst{20-16} = Xm;
  let Inst{12} = idx{2};
  let Inst{11-10} = idx{1-0};
}

class SIMDLdStSingleH<bit L, bit R, bits<3> opcode, bit size, string asm,
                      dag oops, dag iops, list<dag> pattern>
  : BaseSIMDLdStSingle<L, R, opcode, asm, "\t$Vt$idx, [$Rn]", "", oops, iops,
                       pattern> {
  // idx encoded in Q:S:size<1> fields.
  bits<3> idx;
  let Inst{30} = idx{2};
  let Inst{23} = 0;
  let Inst{20-16} = 0b00000;
  let Inst{12} = idx{1};
  let Inst{11} = idx{0};
  let Inst{10} = size;
}
class SIMDLdStSingleHTied<bit L, bit R, bits<3> opcode, bit size, string asm,
                      dag oops, dag iops, list<dag> pattern>
  : BaseSIMDLdStSingleTied<L, R, opcode, asm, "\t$Vt$idx, [$Rn]", "",
                           oops, iops, pattern> {
  // idx encoded in Q:S:size<1> fields.
  bits<3> idx;
  let Inst{30} = idx{2};
  let Inst{23} = 0;
  let Inst{20-16} = 0b00000;
  let Inst{12} = idx{1};
  let Inst{11} = idx{0};
  let Inst{10} = size;
}

class SIMDLdStSingleHPost<bit L, bit R, bits<3> opcode, bit size, string asm,
                          dag oops, dag iops>
  : BaseSIMDLdStSingle<L, R, opcode, asm, "\t$Vt$idx, [$Rn], $Xm",
                       "$Rn = $wback", oops, iops, []> {
  // idx encoded in Q:S:size<1> fields.
  bits<3> idx;
  bits<5> Xm;
  let Inst{30} = idx{2};
  let Inst{23} = 1;
  let Inst{20-16} = Xm;
  let Inst{12} = idx{1};
  let Inst{11} = idx{0};
  let Inst{10} = size;
}
class SIMDLdStSingleHTiedPost<bit L, bit R, bits<3> opcode, bit size, string asm,
                          dag oops, dag iops>
  : BaseSIMDLdStSingleTied<L, R, opcode, asm, "\t$Vt$idx, [$Rn], $Xm",
                           "$Rn = $wback", oops, iops, []> {
  // idx encoded in Q:S:size<1> fields.
  bits<3> idx;
  bits<5> Xm;
  let Inst{30} = idx{2};
  let Inst{23} = 1;
  let Inst{20-16} = Xm;
  let Inst{12} = idx{1};
  let Inst{11} = idx{0};
  let Inst{10} = size;
}
class SIMDLdStSingleS<bit L, bit R, bits<3> opcode, bits<2> size, string asm,
                      dag oops, dag iops, list<dag> pattern>
  : BaseSIMDLdStSingle<L, R, opcode, asm, "\t$Vt$idx, [$Rn]", "", oops, iops,
                       pattern> {
  // idx encoded in Q:S fields.
  bits<2> idx;
  let Inst{30} = idx{1};
  let Inst{23} = 0;
  let Inst{20-16} = 0b00000;
  let Inst{12} = idx{0};
  let Inst{11-10} = size;
}
class SIMDLdStSingleSTied<bit L, bit R, bits<3> opcode, bits<2> size, string asm,
                      dag oops, dag iops, list<dag> pattern>
  : BaseSIMDLdStSingleTied<L, R, opcode, asm, "\t$Vt$idx, [$Rn]", "",
                           oops, iops, pattern> {
  // idx encoded in Q:S fields.
  bits<2> idx;
  let Inst{30} = idx{1};
  let Inst{23} = 0;
  let Inst{20-16} = 0b00000;
  let Inst{12} = idx{0};
  let Inst{11-10} = size;
}
class SIMDLdStSingleSPost<bit L, bit R, bits<3> opcode, bits<2> size,
                          string asm, dag oops, dag iops>
  : BaseSIMDLdStSingle<L, R, opcode, asm, "\t$Vt$idx, [$Rn], $Xm",
                       "$Rn = $wback", oops, iops, []> {
  // idx encoded in Q:S fields.
  bits<2> idx;
  bits<5> Xm;
  let Inst{30} = idx{1};
  let Inst{23} = 1;
  let Inst{20-16} = Xm;
  let Inst{12} = idx{0};
  let Inst{11-10} = size;
}
class SIMDLdStSingleSTiedPost<bit L, bit R, bits<3> opcode, bits<2> size,
                          string asm, dag oops, dag iops>
  : BaseSIMDLdStSingleTied<L, R, opcode, asm, "\t$Vt$idx, [$Rn], $Xm",
                           "$Rn = $wback", oops, iops, []> {
  // idx encoded in Q:S fields.
  bits<2> idx;
  bits<5> Xm;
  let Inst{30} = idx{1};
  let Inst{23} = 1;
  let Inst{20-16} = Xm;
  let Inst{12} = idx{0};
  let Inst{11-10} = size;
}
class SIMDLdStSingleD<bit L, bit R, bits<3> opcode, bits<2> size, string asm,
                      dag oops, dag iops, list<dag> pattern>
  : BaseSIMDLdStSingle<L, R, opcode, asm, "\t$Vt$idx, [$Rn]", "", oops, iops,
                       pattern> {
  // idx encoded in Q field.
  bits<1> idx;
  let Inst{30} = idx;
  let Inst{23} = 0;
  let Inst{20-16} = 0b00000;
  let Inst{12} = 0;
  let Inst{11-10} = size;
}
class SIMDLdStSingleDTied<bit L, bit R, bits<3> opcode, bits<2> size, string asm,
                      dag oops, dag iops, list<dag> pattern>
  : BaseSIMDLdStSingleTied<L, R, opcode, asm, "\t$Vt$idx, [$Rn]", "",
                           oops, iops, pattern> {
  // idx encoded in Q field.
  bits<1> idx;
  let Inst{30} = idx;
  let Inst{23} = 0;
  let Inst{20-16} = 0b00000;
  let Inst{12} = 0;
  let Inst{11-10} = size;
}
class SIMDLdStSingleDPost<bit L, bit R, bits<3> opcode, bits<2> size,
                          string asm, dag oops, dag iops>
  : BaseSIMDLdStSingle<L, R, opcode, asm, "\t$Vt$idx, [$Rn], $Xm",
                       "$Rn = $wback", oops, iops, []> {
  // idx encoded in Q field.
  bits<1> idx;
  bits<5> Xm;
  let Inst{30} = idx;
  let Inst{23} = 1;
  let Inst{20-16} = Xm;
  let Inst{12} = 0;
  let Inst{11-10} = size;
}
class SIMDLdStSingleDTiedPost<bit L, bit R, bits<3> opcode, bits<2> size,
                          string asm, dag oops, dag iops>
  : BaseSIMDLdStSingleTied<L, R, opcode, asm, "\t$Vt$idx, [$Rn], $Xm",
                           "$Rn = $wback", oops, iops, []> {
  // idx encoded in Q field.
  bits<1> idx;
  bits<5> Xm;
  let Inst{30} = idx;
  let Inst{23} = 1;
  let Inst{20-16} = Xm;
  let Inst{12} = 0;
  let Inst{11-10} = size;
}

let mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
multiclass SIMDLdSingleBTied<bit R, bits<3> opcode, string asm,
                         RegisterOperand listtype,
                         RegisterOperand GPR64pi> {
  def i8 : SIMDLdStSingleBTied<1, R, opcode, asm,
                           (outs listtype:$dst),
                           (ins listtype:$Vt, VectorIndexB:$idx,
                                GPR64sp:$Rn), []>;

  def i8_POST : SIMDLdStSingleBTiedPost<1, R, opcode, asm,
                            (outs GPR64sp:$wback, listtype:$dst),
                            (ins listtype:$Vt, VectorIndexB:$idx,
                                 GPR64sp:$Rn, GPR64pi:$Xm)>;
}
let mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
multiclass SIMDLdSingleHTied<bit R, bits<3> opcode, bit size, string asm,
                         RegisterOperand listtype,
                         RegisterOperand GPR64pi> {
  def i16 : SIMDLdStSingleHTied<1, R, opcode, size, asm,
                            (outs listtype:$dst),
                            (ins listtype:$Vt, VectorIndexH:$idx,
                                 GPR64sp:$Rn), []>;

  def i16_POST : SIMDLdStSingleHTiedPost<1, R, opcode, size, asm,
                            (outs GPR64sp:$wback, listtype:$dst),
                            (ins listtype:$Vt, VectorIndexH:$idx,
                                 GPR64sp:$Rn, GPR64pi:$Xm)>;
}
let mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
multiclass SIMDLdSingleSTied<bit R, bits<3> opcode, bits<2> size,string asm,
                         RegisterOperand listtype,
                         RegisterOperand GPR64pi> {
  def i32 : SIMDLdStSingleSTied<1, R, opcode, size, asm,
                            (outs listtype:$dst),
                            (ins listtype:$Vt, VectorIndexS:$idx,
                                 GPR64sp:$Rn), []>;

  def i32_POST : SIMDLdStSingleSTiedPost<1, R, opcode, size, asm,
                            (outs GPR64sp:$wback, listtype:$dst),
                            (ins listtype:$Vt, VectorIndexS:$idx,
                                 GPR64sp:$Rn, GPR64pi:$Xm)>;
}
let mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
multiclass SIMDLdSingleDTied<bit R, bits<3> opcode, bits<2> size, string asm,
                         RegisterOperand listtype, RegisterOperand GPR64pi> {
  def i64 : SIMDLdStSingleDTied<1, R, opcode, size, asm,
                            (outs listtype:$dst),
                            (ins listtype:$Vt, VectorIndexD:$idx,
                                 GPR64sp:$Rn), []>;

  def i64_POST : SIMDLdStSingleDTiedPost<1, R, opcode, size, asm,
                            (outs GPR64sp:$wback, listtype:$dst),
                            (ins listtype:$Vt, VectorIndexD:$idx,
                                 GPR64sp:$Rn, GPR64pi:$Xm)>;
}
let mayLoad = 0, mayStore = 1, hasSideEffects = 0 in
multiclass SIMDStSingleB<bit R, bits<3> opcode, string asm,
                         RegisterOperand listtype, RegisterOperand GPR64pi> {
  def i8 : SIMDLdStSingleB<0, R, opcode, asm,
                           (outs), (ins listtype:$Vt, VectorIndexB:$idx,
                                        GPR64sp:$Rn), []>;

  def i8_POST : SIMDLdStSingleBPost<0, R, opcode, asm,
                                    (outs GPR64sp:$wback),
                                    (ins listtype:$Vt, VectorIndexB:$idx,
                                         GPR64sp:$Rn, GPR64pi:$Xm)>;
}
let mayLoad = 0, mayStore = 1, hasSideEffects = 0 in
multiclass SIMDStSingleH<bit R, bits<3> opcode, bit size, string asm,
                         RegisterOperand listtype, RegisterOperand GPR64pi> {
  def i16 : SIMDLdStSingleH<0, R, opcode, size, asm,
                            (outs), (ins listtype:$Vt, VectorIndexH:$idx,
                                         GPR64sp:$Rn), []>;

  def i16_POST : SIMDLdStSingleHPost<0, R, opcode, size, asm,
                            (outs GPR64sp:$wback),
                            (ins listtype:$Vt, VectorIndexH:$idx,
                                 GPR64sp:$Rn, GPR64pi:$Xm)>;
}
let mayLoad = 0, mayStore = 1, hasSideEffects = 0 in
multiclass SIMDStSingleS<bit R, bits<3> opcode, bits<2> size,string asm,
                         RegisterOperand listtype, RegisterOperand GPR64pi> {
  def i32 : SIMDLdStSingleS<0, R, opcode, size, asm,
                            (outs), (ins listtype:$Vt, VectorIndexS:$idx,
                                         GPR64sp:$Rn), []>;

  def i32_POST : SIMDLdStSingleSPost<0, R, opcode, size, asm,
                            (outs GPR64sp:$wback),
                            (ins listtype:$Vt, VectorIndexS:$idx,
                                 GPR64sp:$Rn, GPR64pi:$Xm)>;
}
let mayLoad = 0, mayStore = 1, hasSideEffects = 0 in
multiclass SIMDStSingleD<bit R, bits<3> opcode, bits<2> size, string asm,
                         RegisterOperand listtype, RegisterOperand GPR64pi> {
  def i64 : SIMDLdStSingleD<0, R, opcode, size, asm,
                            (outs), (ins listtype:$Vt, VectorIndexD:$idx,
                                         GPR64sp:$Rn), []>;

  def i64_POST : SIMDLdStSingleDPost<0, R, opcode, size, asm,
                            (outs GPR64sp:$wback),
                            (ins listtype:$Vt, VectorIndexD:$idx,
                                 GPR64sp:$Rn, GPR64pi:$Xm)>;
}

multiclass SIMDLdStSingleAliases<string asm, string layout, string Type,
                                 string Count, int Offset, Operand idxtype> {
  // E.g. "ld1 { v0.8b }[0], [x1], #1"
  //      "ld1\t$Vt, [$Rn], #1"
  // may get mapped to
  //      (LD1Rv8b_POST VecListOne8b:$Vt, GPR64sp:$Rn, XZR)
  def : InstAlias<asm # "\t$Vt$idx, [$Rn], #" # Offset,
                  (!cast<Instruction>(NAME # Type  # "_POST")
                      GPR64sp:$Rn,
                      !cast<RegisterOperand>("VecList" # Count # layout):$Vt,
                      idxtype:$idx, XZR), 1>;

  // E.g. "ld1.8b { v0 }[0], [x1], #1"
  //      "ld1.8b\t$Vt, [$Rn], #1"
  // may get mapped to
  //      (LD1Rv8b_POST VecListOne64:$Vt, GPR64sp:$Rn, XZR)
  def : InstAlias<asm # "." # layout # "\t$Vt$idx, [$Rn], #" # Offset,
                  (!cast<Instruction>(NAME # Type # "_POST")
                      GPR64sp:$Rn,
                      !cast<RegisterOperand>("VecList" # Count # "128"):$Vt,
                      idxtype:$idx, XZR), 0>;

  // E.g. "ld1.8b { v0 }[0], [x1]"
  //      "ld1.8b\t$Vt, [$Rn]"
  // may get mapped to
  //      (LD1Rv8b VecListOne64:$Vt, GPR64sp:$Rn)
  def : InstAlias<asm # "." # layout # "\t$Vt$idx, [$Rn]",
                      (!cast<Instruction>(NAME # Type)
                         !cast<RegisterOperand>("VecList" # Count # "128"):$Vt,
                         idxtype:$idx, GPR64sp:$Rn), 0>;

  // E.g. "ld1.8b { v0 }[0], [x1], x2"
  //      "ld1.8b\t$Vt, [$Rn], $Xm"
  // may get mapped to
  //      (LD1Rv8b_POST VecListOne64:$Vt, GPR64sp:$Rn, GPR64pi1:$Xm)
  def : InstAlias<asm # "." # layout # "\t$Vt$idx, [$Rn], $Xm",
                      (!cast<Instruction>(NAME # Type # "_POST")
                         GPR64sp:$Rn,
                         !cast<RegisterOperand>("VecList" # Count # "128"):$Vt,
                         idxtype:$idx,
                         !cast<RegisterOperand>("GPR64pi" # Offset):$Xm), 0>;
}

multiclass SIMDLdSt1SingleAliases<string asm> {
  defm "" : SIMDLdStSingleAliases<asm, "b", "i8",  "One", 1, VectorIndexB>;
  defm "" : SIMDLdStSingleAliases<asm, "h", "i16", "One", 2, VectorIndexH>;
  defm "" : SIMDLdStSingleAliases<asm, "s", "i32", "One", 4, VectorIndexS>;
  defm "" : SIMDLdStSingleAliases<asm, "d", "i64", "One", 8, VectorIndexD>;
}

multiclass SIMDLdSt2SingleAliases<string asm> {
  defm "" : SIMDLdStSingleAliases<asm, "b", "i8",  "Two", 2,  VectorIndexB>;
  defm "" : SIMDLdStSingleAliases<asm, "h", "i16", "Two", 4,  VectorIndexH>;
  defm "" : SIMDLdStSingleAliases<asm, "s", "i32", "Two", 8,  VectorIndexS>;
  defm "" : SIMDLdStSingleAliases<asm, "d", "i64", "Two", 16, VectorIndexD>;
}

multiclass SIMDLdSt3SingleAliases<string asm> {
  defm "" : SIMDLdStSingleAliases<asm, "b", "i8",  "Three", 3,  VectorIndexB>;
  defm "" : SIMDLdStSingleAliases<asm, "h", "i16", "Three", 6,  VectorIndexH>;
  defm "" : SIMDLdStSingleAliases<asm, "s", "i32", "Three", 12, VectorIndexS>;
  defm "" : SIMDLdStSingleAliases<asm, "d", "i64", "Three", 24, VectorIndexD>;
}

multiclass SIMDLdSt4SingleAliases<string asm> {
  defm "" : SIMDLdStSingleAliases<asm, "b", "i8",  "Four", 4,  VectorIndexB>;
  defm "" : SIMDLdStSingleAliases<asm, "h", "i16", "Four", 8,  VectorIndexH>;
  defm "" : SIMDLdStSingleAliases<asm, "s", "i32", "Four", 16, VectorIndexS>;
  defm "" : SIMDLdStSingleAliases<asm, "d", "i64", "Four", 32, VectorIndexD>;
}
} // end of 'let Predicates = [HasNEON]'

//----------------------------------------------------------------------------
// AdvSIMD v8.1 Rounding Double Multiply Add/Subtract
//----------------------------------------------------------------------------

let Predicates = [HasNEON, HasRDM] in {

class BaseSIMDThreeSameVectorTiedR0<bit Q, bit U, bits<2> size, bits<5> opcode,
                                    RegisterOperand regtype, string asm,
                                    string kind, list<dag> pattern>
  : BaseSIMDThreeSameVectorTied<Q, U, {size,0}, opcode, regtype, asm, kind,
                                pattern> {
}
multiclass SIMDThreeSameVectorSQRDMLxHTiedHS<bit U, bits<5> opc, string asm,
                                             SDPatternOperator Accum> {
  def v4i16 : BaseSIMDThreeSameVectorTiedR0<0, U, 0b01, opc, V64, asm, ".4h",
    [(set (v4i16 V64:$dst),
          (Accum (v4i16 V64:$Rd),
                 (v4i16 (int_aarch64_neon_sqrdmulh (v4i16 V64:$Rn),
                                                   (v4i16 V64:$Rm)))))]>;
  def v8i16 : BaseSIMDThreeSameVectorTiedR0<1, U, 0b01, opc, V128, asm, ".8h",
    [(set (v8i16 V128:$dst),
          (Accum (v8i16 V128:$Rd),
                 (v8i16 (int_aarch64_neon_sqrdmulh (v8i16 V128:$Rn),
                                                   (v8i16 V128:$Rm)))))]>;
  def v2i32 : BaseSIMDThreeSameVectorTiedR0<0, U, 0b10, opc, V64, asm, ".2s",
    [(set (v2i32 V64:$dst),
          (Accum (v2i32 V64:$Rd),
                 (v2i32 (int_aarch64_neon_sqrdmulh (v2i32 V64:$Rn),
                                                   (v2i32 V64:$Rm)))))]>;
  def v4i32 : BaseSIMDThreeSameVectorTiedR0<1, U, 0b10, opc, V128, asm, ".4s",
    [(set (v4i32 V128:$dst),
          (Accum (v4i32 V128:$Rd),
                 (v4i32 (int_aarch64_neon_sqrdmulh (v4i32 V128:$Rn),
                                                   (v4i32 V128:$Rm)))))]>;
}

multiclass SIMDIndexedSQRDMLxHSDTied<bit U, bits<4> opc, string asm,
                                     SDPatternOperator Accum> {
  def v4i16_indexed : BaseSIMDIndexedTied<0, U, 0, 0b01, opc,
                                          V64, V64, V128_lo, VectorIndexH,
                                          asm, ".4h", ".4h", ".4h", ".h",
    [(set (v4i16 V64:$dst),
          (Accum (v4i16 V64:$Rd),
                 (v4i16 (int_aarch64_neon_sqrdmulh
                          (v4i16 V64:$Rn),
                          (v4i16 (AArch64duplane16 (v8i16 V128_lo:$Rm),
                                                    VectorIndexH:$idx))))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v8i16_indexed : BaseSIMDIndexedTied<1, U, 0, 0b01, opc,
                                          V128, V128, V128_lo, VectorIndexH,
                                          asm, ".8h", ".8h", ".8h", ".h",
    [(set (v8i16 V128:$dst),
          (Accum (v8i16 V128:$Rd),
                 (v8i16 (int_aarch64_neon_sqrdmulh
                          (v8i16 V128:$Rn),
                          (v8i16 (AArch64duplane16 (v8i16 V128_lo:$Rm),
                                                   VectorIndexH:$idx))))))]> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def v2i32_indexed : BaseSIMDIndexedTied<0, U, 0, 0b10, opc,
                                          V64, V64, V128, VectorIndexS,
                                          asm, ".2s", ".2s", ".2s", ".s",
    [(set (v2i32 V64:$dst),
        (Accum (v2i32 V64:$Rd),
               (v2i32 (int_aarch64_neon_sqrdmulh
                        (v2i32 V64:$Rn),
                        (v2i32 (AArch64duplane32 (v4i32 V128:$Rm),
                                                 VectorIndexS:$idx))))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  // FIXME: it would be nice to use the scalar (v1i32) instruction here, but
  // an intermediate EXTRACT_SUBREG would be untyped.
  // FIXME: direct EXTRACT_SUBREG from v2i32 to i32 is illegal, that's why we
  // got it lowered here as (i32 vector_extract (v4i32 insert_subvector(..)))
  def : Pat<(i32 (Accum (i32 FPR32Op:$Rd),
                       (i32 (vector_extract
                               (v4i32 (insert_subvector
                                       (undef),
                                        (v2i32 (int_aarch64_neon_sqrdmulh
                                                 (v2i32 V64:$Rn),
                                                 (v2i32 (AArch64duplane32
                                                          (v4i32 V128:$Rm),
                                                          VectorIndexS:$idx)))),
                                      (i32 0))),
                               (i64 0))))),
            (EXTRACT_SUBREG
                (v2i32 (!cast<Instruction>(NAME # v2i32_indexed)
                          (v2i32 (INSERT_SUBREG (v2i32 (IMPLICIT_DEF)),
                                                FPR32Op:$Rd,
                                                ssub)),
                          V64:$Rn,
                          V128:$Rm,
                          VectorIndexS:$idx)),
                ssub)>;

  def v4i32_indexed : BaseSIMDIndexedTied<1, U, 0, 0b10, opc,
                                          V128, V128, V128, VectorIndexS,
                                          asm, ".4s", ".4s", ".4s", ".s",
    [(set (v4i32 V128:$dst),
          (Accum (v4i32 V128:$Rd),
                 (v4i32 (int_aarch64_neon_sqrdmulh
                          (v4i32 V128:$Rn),
                          (v4i32 (AArch64duplane32 (v4i32 V128:$Rm),
                                                   VectorIndexS:$idx))))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }

  // FIXME: it would be nice to use the scalar (v1i32) instruction here, but
  // an intermediate EXTRACT_SUBREG would be untyped.
  def : Pat<(i32 (Accum (i32 FPR32Op:$Rd),
                        (i32 (vector_extract
                               (v4i32 (int_aarch64_neon_sqrdmulh
                                        (v4i32 V128:$Rn),
                                        (v4i32 (AArch64duplane32
                                                 (v4i32 V128:$Rm),
                                                 VectorIndexS:$idx)))),
                               (i64 0))))),
            (EXTRACT_SUBREG
                (v4i32 (!cast<Instruction>(NAME # v4i32_indexed)
                         (v4i32 (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)),
                                               FPR32Op:$Rd,
                                               ssub)),
                         V128:$Rn,
                         V128:$Rm,
                         VectorIndexS:$idx)),
                ssub)>;

  def i16_indexed : BaseSIMDIndexedTied<1, U, 1, 0b01, opc,
                                        FPR16Op, FPR16Op, V128_lo,
                                        VectorIndexH, asm, ".h", "", "", ".h",
                                        []> {
    bits<3> idx;
    let Inst{11} = idx{2};
    let Inst{21} = idx{1};
    let Inst{20} = idx{0};
  }

  def i32_indexed : BaseSIMDIndexedTied<1, U, 1, 0b10, opc,
                                        FPR32Op, FPR32Op, V128, VectorIndexS,
                                        asm, ".s", "", "", ".s",
    [(set (i32 FPR32Op:$dst),
          (Accum (i32 FPR32Op:$Rd),
                 (i32 (int_aarch64_neon_sqrdmulh
                        (i32 FPR32Op:$Rn),
                        (i32 (vector_extract (v4i32 V128:$Rm),
                                             VectorIndexS:$idx))))))]> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }
}
} // let Predicates = [HasNeon, HasRDM]

//----------------------------------------------------------------------------
// ARMv8.3 Complex ADD/MLA instructions
//----------------------------------------------------------------------------

class ComplexRotationOperand<int Angle, int Remainder, string Type>
  : AsmOperandClass {
  let PredicateMethod = "isComplexRotation<" # Angle # ", " # Remainder # ">";
  let DiagnosticType = "InvalidComplexRotation" # Type;
  let Name = "ComplexRotation" # Type;
}
def complexrotateop : Operand<i32> {
  let ParserMatchClass = ComplexRotationOperand<90, 0, "Even">;
  let PrintMethod = "printComplexRotationOp<90, 0>";
}
def complexrotateopodd : Operand<i32> {
  let ParserMatchClass = ComplexRotationOperand<180, 90, "Odd">;
  let PrintMethod = "printComplexRotationOp<180, 90>";
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDThreeSameVectorComplex<bit Q, bit U, bits<2> size, bits<3> opcode,
                                     RegisterOperand regtype, Operand rottype,
                                     string asm, string kind, list<dag> pattern>
  : I<(outs regtype:$Rd), (ins regtype:$Rn, regtype:$Rm, rottype:$rot), asm,
      "{\t$Rd" # kind # ", $Rn" # kind # ", $Rm" # kind # ", $rot"
      "|" # kind # "\t$Rd, $Rn, $Rm, $rot}", "", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<1> rot;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size;
  let Inst{21}    = 0;
  let Inst{20-16} = Rm;
  let Inst{15-13} = opcode;
  // Non-tied version (FCADD) only has one rotation bit
  let Inst{12}    = rot;
  let Inst{11}    = 0;
  let Inst{10}    = 1;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

//8.3 CompNum - Floating-point complex number support
multiclass SIMDThreeSameVectorComplexHSD<bit U, bits<3> opcode, Operand rottype,
                                          string asm, SDPatternOperator OpNode>{
  let Predicates = [HasComplxNum, HasNEON, HasFullFP16] in {
  def v4f16 : BaseSIMDThreeSameVectorComplex<0, U, 0b01, opcode, V64, rottype,
              asm, ".4h",
              [(set (v4f16 V64:$dst), (OpNode (v4f16 V64:$Rd),
                                              (v4f16 V64:$Rn),
                                              (v4f16 V64:$Rm),
                                              (rottype i32:$rot)))]>;

  def v8f16 : BaseSIMDThreeSameVectorComplex<1, U, 0b01, opcode, V128, rottype,
              asm, ".8h",
              [(set (v8f16 V128:$dst), (OpNode (v8f16 V128:$Rd),
                                               (v8f16 V128:$Rn),
                                               (v8f16 V128:$Rm),
                                               (rottype i32:$rot)))]>;
  }

  let Predicates = [HasComplxNum, HasNEON] in {
  def v2f32 : BaseSIMDThreeSameVectorComplex<0, U, 0b10, opcode, V64, rottype,
              asm, ".2s",
              [(set (v2f32 V64:$dst), (OpNode (v2f32 V64:$Rd),
                                              (v2f32 V64:$Rn),
                                              (v2f32 V64:$Rm),
                                              (rottype i32:$rot)))]>;

  def v4f32 : BaseSIMDThreeSameVectorComplex<1, U, 0b10, opcode, V128, rottype,
              asm, ".4s",
              [(set (v4f32 V128:$dst), (OpNode (v4f32 V128:$Rd),
                                               (v4f32 V128:$Rn),
                                               (v4f32 V128:$Rm),
                                               (rottype i32:$rot)))]>;

  def v2f64 : BaseSIMDThreeSameVectorComplex<1, U, 0b11, opcode, V128, rottype,
              asm, ".2d",
              [(set (v2f64 V128:$dst), (OpNode (v2f64 V128:$Rd),
                                               (v2f64 V128:$Rn),
                                               (v2f64 V128:$Rm),
                                               (rottype i32:$rot)))]>;
  }
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDThreeSameVectorTiedComplex<bit Q, bit U, bits<2> size,
                                         bits<3> opcode,
                                         RegisterOperand regtype,
                                         Operand rottype, string asm,
                                         string kind, list<dag> pattern>
  : I<(outs regtype:$dst),
      (ins regtype:$Rd, regtype:$Rn, regtype:$Rm, rottype:$rot), asm,
      "{\t$Rd" # kind # ", $Rn" # kind # ", $Rm" # kind # ", $rot"
      "|" # kind # "\t$Rd, $Rn, $Rm, $rot}", "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<2> rot;
  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28-24} = 0b01110;
  let Inst{23-22} = size;
  let Inst{21}    = 0;
  let Inst{20-16} = Rm;
  let Inst{15-13} = opcode;
  let Inst{12-11} = rot;
  let Inst{10}    = 1;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

multiclass SIMDThreeSameVectorTiedComplexHSD<bit U, bits<3> opcode,
                                             Operand rottype, string asm,
                                             SDPatternOperator OpNode> {
  let Predicates = [HasComplxNum, HasNEON, HasFullFP16] in {
  def v4f16 : BaseSIMDThreeSameVectorTiedComplex<0, U, 0b01, opcode, V64,
              rottype, asm, ".4h",
              [(set (v4f16 V64:$dst), (OpNode (v4f16 V64:$Rd),
                                              (v4f16 V64:$Rn),
                                              (v4f16 V64:$Rm),
                                              (rottype i32:$rot)))]>;

  def v8f16 : BaseSIMDThreeSameVectorTiedComplex<1, U, 0b01, opcode, V128,
              rottype, asm, ".8h",
              [(set (v8f16 V128:$dst), (OpNode (v8f16 V128:$Rd),
                                               (v8f16 V128:$Rn),
                                               (v8f16 V128:$Rm),
                                               (rottype i32:$rot)))]>;
  }

  let Predicates = [HasComplxNum, HasNEON] in {
  def v2f32 : BaseSIMDThreeSameVectorTiedComplex<0, U, 0b10, opcode, V64,
              rottype, asm, ".2s",
              [(set (v2f32 V64:$dst), (OpNode (v2f32 V64:$Rd),
                                              (v2f32 V64:$Rn),
                                              (v2f32 V64:$Rm),
                                              (rottype i32:$rot)))]>;

  def v4f32 : BaseSIMDThreeSameVectorTiedComplex<1, U, 0b10, opcode, V128,
              rottype, asm, ".4s",
              [(set (v4f32 V128:$dst), (OpNode (v4f32 V128:$Rd),
                                               (v4f32 V128:$Rn),
                                               (v4f32 V128:$Rm),
                                               (rottype i32:$rot)))]>;

  def v2f64 : BaseSIMDThreeSameVectorTiedComplex<1, U, 0b11, opcode, V128,
              rottype, asm, ".2d",
              [(set (v2f64 V128:$dst), (OpNode (v2f64 V128:$Rd),
                                               (v2f64 V128:$Rn),
                                               (v2f64 V128:$Rm),
                                               (rottype i32:$rot)))]>;
  }
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class BaseSIMDIndexedTiedComplex<bit Q, bit U, bit Scalar, bits<2> size,
                                 bit opc1, bit opc2, RegisterOperand dst_reg,
                                 RegisterOperand lhs_reg,
                                 RegisterOperand rhs_reg, Operand vec_idx,
                                 Operand rottype, string asm, string apple_kind,
                                 string dst_kind, string lhs_kind,
                                 string rhs_kind, list<dag> pattern>
  : I<(outs dst_reg:$dst),
      (ins dst_reg:$Rd, lhs_reg:$Rn, rhs_reg:$Rm, vec_idx:$idx, rottype:$rot),
      asm,
      "{\t$Rd" # dst_kind # ", $Rn" # lhs_kind # ", $Rm" # rhs_kind #
      "$idx, $rot" # "|" # apple_kind #
      "\t$Rd, $Rn, $Rm$idx, $rot}", "$Rd = $dst", pattern>,
    Sched<[WriteV]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  bits<2> rot;

  let Inst{31}    = 0;
  let Inst{30}    = Q;
  let Inst{29}    = U;
  let Inst{28}    = Scalar;
  let Inst{27-24} = 0b1111;
  let Inst{23-22} = size;
  // Bit 21 must be set by the derived class.
  let Inst{20-16} = Rm;
  let Inst{15}    = opc1;
  let Inst{14-13} = rot;
  let Inst{12}    = opc2;
  // Bit 11 must be set by the derived class.
  let Inst{10}    = 0;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

// The complex instructions index by pairs of elements, so the VectorIndexes
// don't match the lane types, and the index bits are different to the other
// classes.
multiclass SIMDIndexedTiedComplexHSD<bit U, bit opc1, bit opc2, Operand rottype,
                                     string asm, SDPatternOperator OpNode> {
  let Predicates = [HasComplxNum, HasNEON, HasFullFP16] in {
  def v4f16_indexed : BaseSIMDIndexedTiedComplex<0, 1, 0, 0b01, opc1, opc2, V64,
                      V64, V128, VectorIndexD, rottype, asm, ".4h", ".4h",
                      ".4h", ".h", []> {
    bits<1> idx;
    let Inst{11} = 0;
    let Inst{21} = idx{0};
  }

  def v8f16_indexed : BaseSIMDIndexedTiedComplex<1, 1, 0, 0b01, opc1, opc2,
                      V128, V128, V128, VectorIndexS, rottype, asm, ".8h",
                      ".8h", ".8h", ".h", []> {
    bits<2> idx;
    let Inst{11} = idx{1};
    let Inst{21} = idx{0};
  }
  } // Predicates = HasComplxNum, HasNEON, HasFullFP16]

  let Predicates = [HasComplxNum, HasNEON] in {
  def v4f32_indexed : BaseSIMDIndexedTiedComplex<1, 1, 0, 0b10, opc1, opc2,
                      V128, V128, V128, VectorIndexD, rottype, asm, ".4s",
                      ".4s", ".4s", ".s", []> {
    bits<1> idx;
    let Inst{11} = idx{0};
    let Inst{21} = 0;
  }
  } // Predicates = [HasComplxNum, HasNEON]
}

//----------------------------------------------------------------------------
// Crypto extensions
//----------------------------------------------------------------------------

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class AESBase<bits<4> opc, string asm, dag outs, dag ins, string cstr,
              list<dag> pat>
  : I<outs, ins, asm, "{\t$Rd.16b, $Rn.16b|.16b\t$Rd, $Rn}", cstr, pat>,
    Sched<[WriteV]>{
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31-16} = 0b0100111000101000;
  let Inst{15-12} = opc;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

class AESInst<bits<4> opc, string asm, Intrinsic OpNode>
  : AESBase<opc, asm, (outs V128:$Rd), (ins V128:$Rn), "",
            [(set (v16i8 V128:$Rd), (OpNode (v16i8 V128:$Rn)))]>;

class AESTiedInst<bits<4> opc, string asm, Intrinsic OpNode>
  : AESBase<opc, asm, (outs V128:$dst), (ins V128:$Rd, V128:$Rn),
            "$Rd = $dst",
            [(set (v16i8 V128:$dst),
                  (OpNode (v16i8 V128:$Rd), (v16i8 V128:$Rn)))]>;

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class SHA3OpTiedInst<bits<3> opc, string asm, string dst_lhs_kind,
                     dag oops, dag iops, list<dag> pat>
  : I<oops, iops, asm,
      "{\t$Rd" # dst_lhs_kind # ", $Rn" # dst_lhs_kind # ", $Rm.4s" #
      "|.4s\t$Rd, $Rn, $Rm}", "$Rd = $dst", pat>,
    Sched<[WriteV]>{
  bits<5> Rd;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31-21} = 0b01011110000;
  let Inst{20-16} = Rm;
  let Inst{15}    = 0;
  let Inst{14-12} = opc;
  let Inst{11-10} = 0b00;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

class SHATiedInstQSV<bits<3> opc, string asm, Intrinsic OpNode>
  : SHA3OpTiedInst<opc, asm, "", (outs FPR128:$dst),
                   (ins FPR128:$Rd, FPR32:$Rn, V128:$Rm),
                   [(set (v4i32 FPR128:$dst),
                         (OpNode (v4i32 FPR128:$Rd), (i32 FPR32:$Rn),
                                 (v4i32 V128:$Rm)))]>;

class SHATiedInstVVV<bits<3> opc, string asm, Intrinsic OpNode>
  : SHA3OpTiedInst<opc, asm, ".4s", (outs V128:$dst),
                   (ins V128:$Rd, V128:$Rn, V128:$Rm),
                   [(set (v4i32 V128:$dst),
                         (OpNode (v4i32 V128:$Rd), (v4i32 V128:$Rn),
                                 (v4i32 V128:$Rm)))]>;

class SHATiedInstQQV<bits<3> opc, string asm, Intrinsic OpNode>
  : SHA3OpTiedInst<opc, asm, "", (outs FPR128:$dst),
                   (ins FPR128:$Rd, FPR128:$Rn, V128:$Rm),
                   [(set (v4i32 FPR128:$dst),
                         (OpNode (v4i32 FPR128:$Rd), (v4i32 FPR128:$Rn),
                                 (v4i32 V128:$Rm)))]>;

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in
class SHA2OpInst<bits<4> opc, string asm, string kind,
                 string cstr, dag oops, dag iops,
                 list<dag> pat>
  : I<oops, iops, asm, "{\t$Rd" # kind # ", $Rn" # kind #
                       "|" # kind # "\t$Rd, $Rn}", cstr, pat>,
    Sched<[WriteV]>{
  bits<5> Rd;
  bits<5> Rn;
  let Inst{31-16} = 0b0101111000101000;
  let Inst{15-12} = opc;
  let Inst{11-10} = 0b10;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Rd;
}

class SHATiedInstVV<bits<4> opc, string asm, Intrinsic OpNode>
  : SHA2OpInst<opc, asm, ".4s", "$Rd = $dst", (outs V128:$dst),
               (ins V128:$Rd, V128:$Rn),
               [(set (v4i32 V128:$dst),
                     (OpNode (v4i32 V128:$Rd), (v4i32 V128:$Rn)))]>;

class SHAInstSS<bits<4> opc, string asm, Intrinsic OpNode>
  : SHA2OpInst<opc, asm, "", "", (outs FPR32:$Rd), (ins FPR32:$Rn),
               [(set (i32 FPR32:$Rd), (OpNode (i32 FPR32:$Rn)))]>;

// Armv8.2-A Crypto extensions
class BaseCryptoV82<dag oops, dag iops, string asm, string asmops, string cst,
                    list<dag> pattern>
  : I <oops, iops, asm, asmops, cst, pattern>, Sched<[WriteV]> {
  bits<5> Vd;
  bits<5> Vn;
  let Inst{31-25} = 0b1100111;
  let Inst{9-5}   = Vn;
  let Inst{4-0}   = Vd;
}

class CryptoRRTied<bits<1>op0, bits<2>op1, string asm, string asmops>
  : BaseCryptoV82<(outs V128:$Vd), (ins V128:$Vn, V128:$Vm), asm, asmops,
                  "$Vm = $Vd", []> {
  let Inst{31-25} = 0b1100111;
  let Inst{24-21} = 0b0110;
  let Inst{20-15} = 0b000001;
  let Inst{14}    = op0;
  let Inst{13-12} = 0b00;
  let Inst{11-10} = op1;
}
class CryptoRRTied_2D<bits<1>op0, bits<2>op1, string asm>
  : CryptoRRTied<op0, op1, asm, "{\t$Vd.2d, $Vn.2d}">;
class CryptoRRTied_4S<bits<1>op0, bits<2>op1, string asm>
  : CryptoRRTied<op0, op1, asm, "{\t$Vd.4s, $Vn.4s}">;

class CryptoRRR<bits<1> op0, bits<2>op1, dag oops, dag iops, string asm,
                string asmops, string cst>
  : BaseCryptoV82<oops, iops, asm , asmops, cst, []> {
  bits<5> Vm;
  let Inst{24-21} = 0b0011;
  let Inst{20-16} = Vm;
  let Inst{15}    = 0b1;
  let Inst{14}    = op0;
  let Inst{13-12} = 0b00;
  let Inst{11-10} = op1;
}
class CryptoRRR_2D<bits<1> op0, bits<2>op1, string asm>
  : CryptoRRR<op0, op1, (outs V128:$Vd), (ins V128:$Vn, V128:$Vm), asm,
              "{\t$Vd.2d, $Vn.2d, $Vm.2d}", "">;
class CryptoRRRTied_2D<bits<1> op0, bits<2>op1, string asm>
  : CryptoRRR<op0, op1, (outs V128:$Vdst), (ins V128:$Vd, V128:$Vn, V128:$Vm), asm,
              "{\t$Vd.2d, $Vn.2d, $Vm.2d}", "$Vd = $Vdst">;
class CryptoRRR_4S<bits<1> op0, bits<2>op1, string asm>
  : CryptoRRR<op0, op1, (outs V128:$Vd), (ins V128:$Vn, V128:$Vm), asm,
              "{\t$Vd.4s, $Vn.4s, $Vm.4s}", "">;
class CryptoRRRTied_4S<bits<1> op0, bits<2>op1, string asm>
  : CryptoRRR<op0, op1, (outs V128:$Vdst), (ins V128:$Vd, V128:$Vn, V128:$Vm), asm,
              "{\t$Vd.4s, $Vn.4s, $Vm.4s}", "$Vd = $Vdst">;
class CryptoRRRTied<bits<1> op0, bits<2>op1, string asm>
  : CryptoRRR<op0, op1, (outs FPR128:$Vdst), (ins FPR128:$Vd, FPR128:$Vn, V128:$Vm),
              asm, "{\t$Vd, $Vn, $Vm.2d}", "$Vd = $Vdst">;

class CryptoRRRR<bits<2>op0, string asm, string asmops>
  : BaseCryptoV82<(outs V128:$Vd), (ins V128:$Vn, V128:$Vm, V128:$Va), asm,
                  asmops, "", []> {
  bits<5> Vm;
  bits<5> Va;
  let Inst{24-23} = 0b00;
  let Inst{22-21} = op0;
  let Inst{20-16} = Vm;
  let Inst{15}    = 0b0;
  let Inst{14-10} = Va;
}
class CryptoRRRR_16B<bits<2>op0, string asm>
 : CryptoRRRR<op0, asm, "{\t$Vd.16b, $Vn.16b, $Vm.16b, $Va.16b}"> {
}
class CryptoRRRR_4S<bits<2>op0, string asm>
 : CryptoRRRR<op0, asm, "{\t$Vd.4s, $Vn.4s, $Vm.4s, $Va.4s}"> {
}

class CryptoRRRi6<string asm>
  : BaseCryptoV82<(outs V128:$Vd), (ins V128:$Vn, V128:$Vm, uimm6:$imm), asm,
                  "{\t$Vd.2d, $Vn.2d, $Vm.2d, $imm}", "", []> {
  bits<6> imm;
  bits<5> Vm;
  let Inst{24-21} = 0b0100;
  let Inst{20-16} = Vm;
  let Inst{15-10} = imm;
  let Inst{9-5}   = Vn;
  let Inst{4-0}   = Vd;
}

class CryptoRRRi2Tied<bits<1>op0, bits<2>op1, string asm>
  : BaseCryptoV82<(outs V128:$Vdst),
                  (ins V128:$Vd, V128:$Vn, V128:$Vm, VectorIndexS:$imm),
                  asm, "{\t$Vd.4s, $Vn.4s, $Vm.s$imm}", "$Vd = $Vdst", []> {
  bits<2> imm;
  bits<5> Vm;
  let Inst{24-21} = 0b0010;
  let Inst{20-16} = Vm;
  let Inst{15}    = 0b1;
  let Inst{14}    = op0;
  let Inst{13-12} = imm;
  let Inst{11-10} = op1;
}

//----------------------------------------------------------------------------
// v8.1 atomic instructions extension:
// * CAS
// * CASP
// * SWP
// * LDOPregister<OP>, and aliases STOPregister<OP>

// Instruction encodings:
//
//      31 30|29  24|23|22|21|20 16|15|14  10|9 5|4 0
// CAS  SZ   |001000|1 |A |1 |Rs   |R |11111 |Rn |Rt
// CASP  0|SZ|001000|0 |A |1 |Rs   |R |11111 |Rn |Rt
// SWP  SZ   |111000|A |R |1 |Rs   |1 |OPC|00|Rn |Rt
// LD   SZ   |111000|A |R |1 |Rs   |0 |OPC|00|Rn |Rt
// ST   SZ   |111000|A |R |1 |Rs   |0 |OPC|00|Rn |11111

// Instruction syntax:
//
// CAS{<order>}[<size>] <Ws>, <Wt>, [<Xn|SP>]
// CAS{<order>} <Xs>, <Xt>, [<Xn|SP>]
// CASP{<order>} <Ws>, <W(s+1)>, <Wt>, <W(t+1)>, [<Xn|SP>]
// CASP{<order>} <Xs>, <X(s+1)>, <Xt>, <X(t+1)>, [<Xn|SP>]
// SWP{<order>}[<size>] <Ws>, <Wt>, [<Xn|SP>]
// SWP{<order>} <Xs>, <Xt>, [<Xn|SP>]
// LD<OP>{<order>}[<size>] <Ws>, <Wt>, [<Xn|SP>]
// LD<OP>{<order>} <Xs>, <Xt>, [<Xn|SP>]
// ST<OP>{<order>}[<size>] <Ws>, [<Xn|SP>]
// ST<OP>{<order>} <Xs>, [<Xn|SP>]

let Predicates = [HasLSE], mayLoad = 1, mayStore = 1, hasSideEffects = 1 in
class BaseCASEncoding<dag oops, dag iops, string asm, string operands,
                      string cstr, list<dag> pattern>
      : I<oops, iops, asm, operands, cstr, pattern> {
  bits<2> Sz;
  bit NP;
  bit Acq;
  bit Rel;
  bits<5> Rs;
  bits<5> Rn;
  bits<5> Rt;
  let Inst{31-30} = Sz;
  let Inst{29-24} = 0b001000;
  let Inst{23} = NP;
  let Inst{22} = Acq;
  let Inst{21} = 0b1;
  let Inst{20-16} = Rs;
  let Inst{15} = Rel;
  let Inst{14-10} = 0b11111;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rt;
  let Predicates = [HasLSE];
}

class BaseCAS<string order, string size, RegisterClass RC>
      : BaseCASEncoding<(outs RC:$out),(ins RC:$Rs, RC:$Rt, GPR64sp:$Rn),
                        "cas" # order # size, "\t$Rs, $Rt, [$Rn]",
                        "$out = $Rs",[]>,
        Sched<[WriteAtomic]> {
  let NP = 1;
}

multiclass CompareAndSwap<bits<1> Acq, bits<1> Rel, string order> {
  let Sz = 0b00, Acq = Acq, Rel = Rel in def B : BaseCAS<order, "b", GPR32>;
  let Sz = 0b01, Acq = Acq, Rel = Rel in def H : BaseCAS<order, "h", GPR32>;
  let Sz = 0b10, Acq = Acq, Rel = Rel in def W : BaseCAS<order, "", GPR32>;
  let Sz = 0b11, Acq = Acq, Rel = Rel in def X : BaseCAS<order, "", GPR64>;
}

class BaseCASP<string order, string size, RegisterOperand RC>
      : BaseCASEncoding<(outs RC:$out),(ins RC:$Rs, RC:$Rt, GPR64sp:$Rn),
                        "casp" # order # size, "\t$Rs, $Rt, [$Rn]",
                        "$out = $Rs",[]>,
        Sched<[WriteAtomic]> {
  let NP = 0;
}

multiclass CompareAndSwapPair<bits<1> Acq, bits<1> Rel, string order> {
  let Sz = 0b00, Acq = Acq, Rel = Rel in
    def W : BaseCASP<order, "", WSeqPairClassOperand>;
  let Sz = 0b01, Acq = Acq, Rel = Rel in
    def X : BaseCASP<order, "", XSeqPairClassOperand>;
}

let Predicates = [HasLSE] in
class BaseSWP<string order, string size, RegisterClass RC>
      : I<(outs RC:$Rt),(ins RC:$Rs, GPR64sp:$Rn), "swp" # order # size,
          "\t$Rs, $Rt, [$Rn]","",[]>,
        Sched<[WriteAtomic]> {
  bits<2> Sz;
  bit Acq;
  bit Rel;
  bits<5> Rs;
  bits<3> opc = 0b000;
  bits<5> Rn;
  bits<5> Rt;
  let Inst{31-30} = Sz;
  let Inst{29-24} = 0b111000;
  let Inst{23} = Acq;
  let Inst{22} = Rel;
  let Inst{21} = 0b1;
  let Inst{20-16} = Rs;
  let Inst{15} = 0b1;
  let Inst{14-12} = opc;
  let Inst{11-10} = 0b00;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rt;
  let Predicates = [HasLSE];
}

multiclass Swap<bits<1> Acq, bits<1> Rel, string order> {
  let Sz = 0b00, Acq = Acq, Rel = Rel in def B : BaseSWP<order, "b", GPR32>;
  let Sz = 0b01, Acq = Acq, Rel = Rel in def H : BaseSWP<order, "h", GPR32>;
  let Sz = 0b10, Acq = Acq, Rel = Rel in def W : BaseSWP<order, "", GPR32>;
  let Sz = 0b11, Acq = Acq, Rel = Rel in def X : BaseSWP<order, "", GPR64>;
}

let Predicates = [HasLSE], mayLoad = 1, mayStore = 1, hasSideEffects = 1 in
class BaseLDOPregister<string op, string order, string size, RegisterClass RC>
      : I<(outs RC:$Rt),(ins RC:$Rs, GPR64sp:$Rn), "ld" # op # order # size,
          "\t$Rs, $Rt, [$Rn]","",[]>,
        Sched<[WriteAtomic]> {
  bits<2> Sz;
  bit Acq;
  bit Rel;
  bits<5> Rs;
  bits<3> opc;
  bits<5> Rn;
  bits<5> Rt;
  let Inst{31-30} = Sz;
  let Inst{29-24} = 0b111000;
  let Inst{23} = Acq;
  let Inst{22} = Rel;
  let Inst{21} = 0b1;
  let Inst{20-16} = Rs;
  let Inst{15} = 0b0;
  let Inst{14-12} = opc;
  let Inst{11-10} = 0b00;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rt;
  let Predicates = [HasLSE];
}

multiclass LDOPregister<bits<3> opc, string op, bits<1> Acq, bits<1> Rel,
                        string order> {
  let Sz = 0b00, Acq = Acq, Rel = Rel, opc = opc in
    def B : BaseLDOPregister<op, order, "b", GPR32>;
  let Sz = 0b01, Acq = Acq, Rel = Rel, opc = opc in
    def H : BaseLDOPregister<op, order, "h", GPR32>;
  let Sz = 0b10, Acq = Acq, Rel = Rel, opc = opc in
    def W : BaseLDOPregister<op, order, "", GPR32>;
  let Sz = 0b11, Acq = Acq, Rel = Rel, opc = opc in
    def X : BaseLDOPregister<op, order, "", GPR64>;
}

// Differing SrcRHS and DstRHS allow you to cover CLR & SUB by giving a more
// complex DAG for DstRHS.
let Predicates = [HasLSE] in
multiclass LDOPregister_patterns_ord_dag<string inst, string suffix, string op,
                                         string size, dag SrcRHS, dag DstRHS> {
  def : Pat<(!cast<PatFrag>(op#"_"#size#"_monotonic") GPR64sp:$Rn, SrcRHS),
            (!cast<Instruction>(inst # suffix) DstRHS, GPR64sp:$Rn)>;
  def : Pat<(!cast<PatFrag>(op#"_"#size#"_acquire") GPR64sp:$Rn, SrcRHS),
            (!cast<Instruction>(inst # "A" # suffix) DstRHS, GPR64sp:$Rn)>;
  def : Pat<(!cast<PatFrag>(op#"_"#size#"_release") GPR64sp:$Rn, SrcRHS),
            (!cast<Instruction>(inst # "L" # suffix) DstRHS, GPR64sp:$Rn)>;
  def : Pat<(!cast<PatFrag>(op#"_"#size#"_acq_rel") GPR64sp:$Rn, SrcRHS),
            (!cast<Instruction>(inst # "AL" # suffix) DstRHS, GPR64sp:$Rn)>;
  def : Pat<(!cast<PatFrag>(op#"_"#size#"_seq_cst") GPR64sp:$Rn, SrcRHS),
            (!cast<Instruction>(inst # "AL" # suffix) DstRHS, GPR64sp:$Rn)>;
}

multiclass LDOPregister_patterns_ord<string inst, string suffix, string op,
                                     string size, dag RHS> {
  defm : LDOPregister_patterns_ord_dag<inst, suffix, op, size, RHS, RHS>;
}

multiclass LDOPregister_patterns_ord_mod<string inst, string suffix, string op,
                                         string size, dag LHS, dag RHS> {
  defm : LDOPregister_patterns_ord_dag<inst, suffix, op, size, LHS, RHS>;
}

multiclass LDOPregister_patterns<string inst, string op> {
  defm : LDOPregister_patterns_ord<inst, "X", op, "64", (i64 GPR64:$Rm)>;
  defm : LDOPregister_patterns_ord<inst, "W", op, "32", (i32 GPR32:$Rm)>;
  defm : LDOPregister_patterns_ord<inst, "H", op, "16", (i32 GPR32:$Rm)>;
  defm : LDOPregister_patterns_ord<inst, "B", op, "8",  (i32 GPR32:$Rm)>;
}

multiclass LDOPregister_patterns_mod<string inst, string op, string mod> {
  defm : LDOPregister_patterns_ord_mod<inst, "X", op, "64",
                        (i64 GPR64:$Rm),
                        (i64 (!cast<Instruction>(mod#Xrr) XZR, GPR64:$Rm))>;
  defm : LDOPregister_patterns_ord_mod<inst, "W", op, "32",
                        (i32 GPR32:$Rm),
                        (i32 (!cast<Instruction>(mod#Wrr) WZR, GPR32:$Rm))>;
  defm : LDOPregister_patterns_ord_mod<inst, "H", op, "16",
                        (i32 GPR32:$Rm),
                        (i32 (!cast<Instruction>(mod#Wrr) WZR, GPR32:$Rm))>;
  defm : LDOPregister_patterns_ord_mod<inst, "B", op, "8",
                        (i32 GPR32:$Rm),
                        (i32 (!cast<Instruction>(mod#Wrr) WZR, GPR32:$Rm))>;
}

let Predicates = [HasLSE] in
multiclass CASregister_patterns_ord_dag<string inst, string suffix, string op,
                                        string size, dag OLD, dag NEW> {
  def : Pat<(!cast<PatFrag>(op#"_"#size#"_monotonic") GPR64sp:$Rn, OLD, NEW),
            (!cast<Instruction>(inst # suffix) OLD, NEW, GPR64sp:$Rn)>;
  def : Pat<(!cast<PatFrag>(op#"_"#size#"_acquire") GPR64sp:$Rn, OLD, NEW),
            (!cast<Instruction>(inst # "A" # suffix) OLD, NEW, GPR64sp:$Rn)>;
  def : Pat<(!cast<PatFrag>(op#"_"#size#"_release") GPR64sp:$Rn, OLD, NEW),
            (!cast<Instruction>(inst # "L" # suffix) OLD, NEW, GPR64sp:$Rn)>;
  def : Pat<(!cast<PatFrag>(op#"_"#size#"_acq_rel") GPR64sp:$Rn, OLD, NEW),
            (!cast<Instruction>(inst # "AL" # suffix) OLD, NEW, GPR64sp:$Rn)>;
  def : Pat<(!cast<PatFrag>(op#"_"#size#"_seq_cst") GPR64sp:$Rn, OLD, NEW),
            (!cast<Instruction>(inst # "AL" # suffix) OLD, NEW, GPR64sp:$Rn)>;
}

multiclass CASregister_patterns_ord<string inst, string suffix, string op,
                                    string size, dag OLD, dag NEW> {
  defm : CASregister_patterns_ord_dag<inst, suffix, op, size, OLD, NEW>;
}

multiclass CASregister_patterns<string inst, string op> {
  defm : CASregister_patterns_ord<inst, "X", op, "64",
                        (i64 GPR64:$Rold), (i64 GPR64:$Rnew)>;
  defm : CASregister_patterns_ord<inst, "W", op, "32",
                        (i32 GPR32:$Rold), (i32 GPR32:$Rnew)>;
  defm : CASregister_patterns_ord<inst, "H", op, "16",
                        (i32 GPR32:$Rold), (i32 GPR32:$Rnew)>;
  defm : CASregister_patterns_ord<inst, "B", op, "8",
                        (i32 GPR32:$Rold), (i32 GPR32:$Rnew)>;
}

let Predicates = [HasLSE] in
class BaseSTOPregister<string asm, RegisterClass OP, Register Reg,
                        Instruction inst> :
      InstAlias<asm # "\t$Rs, [$Rn]", (inst Reg, OP:$Rs, GPR64sp:$Rn)>;

multiclass STOPregister<string asm, string instr> {
  def : BaseSTOPregister<asm # "lb", GPR32, WZR,
                    !cast<Instruction>(instr # "LB")>;
  def : BaseSTOPregister<asm # "lh", GPR32, WZR,
                    !cast<Instruction>(instr # "LH")>;
  def : BaseSTOPregister<asm # "l",  GPR32, WZR,
                    !cast<Instruction>(instr # "LW")>;
  def : BaseSTOPregister<asm # "l",  GPR64, XZR,
                    !cast<Instruction>(instr # "LX")>;
  def : BaseSTOPregister<asm # "b",  GPR32, WZR,
                    !cast<Instruction>(instr # "B")>;
  def : BaseSTOPregister<asm # "h",  GPR32, WZR,
                    !cast<Instruction>(instr # "H")>;
  def : BaseSTOPregister<asm,        GPR32, WZR,
                    !cast<Instruction>(instr # "W")>;
  def : BaseSTOPregister<asm,        GPR64, XZR,
                    !cast<Instruction>(instr # "X")>;
}

//----------------------------------------------------------------------------
// Allow the size specifier tokens to be upper case, not just lower.
def : TokenAlias<".4B", ".4b">;  // Add dot product
def : TokenAlias<".8B", ".8b">;
def : TokenAlias<".4H", ".4h">;
def : TokenAlias<".2S", ".2s">;
def : TokenAlias<".1D", ".1d">;
def : TokenAlias<".16B", ".16b">;
def : TokenAlias<".8H", ".8h">;
def : TokenAlias<".4S", ".4s">;
def : TokenAlias<".2D", ".2d">;
def : TokenAlias<".1Q", ".1q">;
def : TokenAlias<".2H", ".2h">;
def : TokenAlias<".B", ".b">;
def : TokenAlias<".H", ".h">;
def : TokenAlias<".S", ".s">;
def : TokenAlias<".D", ".d">;
def : TokenAlias<".Q", ".q">;
