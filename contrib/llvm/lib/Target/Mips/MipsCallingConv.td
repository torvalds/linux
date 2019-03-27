//===-- MipsCallingConv.td - Calling Conventions for Mips --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for Mips architecture.
//===----------------------------------------------------------------------===//

/// CCIfSubtarget - Match if the current subtarget has a feature F.
class CCIfSubtarget<string F, CCAction A, string Invert = "">
    : CCIf<!strconcat(Invert,
                      "static_cast<const MipsSubtarget&>"
			"(State.getMachineFunction().getSubtarget()).",
                      F),
           A>;

// The inverse of CCIfSubtarget
class CCIfSubtargetNot<string F, CCAction A> : CCIfSubtarget<F, A, "!">;

/// Match if the original argument (before lowering) was a float.
/// For example, this is true for i32's that were lowered from soft-float.
class CCIfOrigArgWasNotFloat<CCAction A>
    : CCIf<"!static_cast<MipsCCState *>(&State)->WasOriginalArgFloat(ValNo)",
           A>;

/// Match if the original argument (before lowering) was a 128-bit float (i.e.
/// long double).
class CCIfOrigArgWasF128<CCAction A>
    : CCIf<"static_cast<MipsCCState *>(&State)->WasOriginalArgF128(ValNo)", A>;

/// Match if this specific argument is a vararg.
/// This is slightly different fro CCIfIsVarArg which matches if any argument is
/// a vararg.
class CCIfArgIsVarArg<CCAction A>
    : CCIf<"!static_cast<MipsCCState *>(&State)->IsCallOperandFixed(ValNo)", A>;

/// Match if the return was a floating point vector.
class CCIfOrigArgWasNotVectorFloat<CCAction A>
    : CCIf<"!static_cast<MipsCCState *>(&State)"
                "->WasOriginalRetVectorFloat(ValNo)", A>;

/// Match if the special calling conv is the specified value.
class CCIfSpecialCallingConv<string CC, CCAction A>
    : CCIf<"static_cast<MipsCCState *>(&State)->getSpecialCallingConv() == "
               "MipsCCState::" # CC, A>;

// For soft-float, f128 values are returned in A0_64 rather than V1_64.
def RetCC_F128SoftFloat : CallingConv<[
  CCAssignToReg<[V0_64, A0_64]>
]>;

// For hard-float, f128 values are returned as a pair of f64's rather than a
// pair of i64's.
def RetCC_F128HardFloat : CallingConv<[
  CCBitConvertToType<f64>,

  // Contrary to the ABI documentation, a struct containing a long double is
  // returned in $f0, and $f1 instead of the usual $f0, and $f2. This is to
  // match the de facto ABI as implemented by GCC.
  CCIfInReg<CCAssignToReg<[D0_64, D1_64]>>,

  CCAssignToReg<[D0_64, D2_64]>
]>;

// Handle F128 specially since we can't identify the original type during the
// tablegen-erated code.
def RetCC_F128 : CallingConv<[
  CCIfSubtarget<"useSoftFloat()",
      CCIfType<[i64], CCDelegateTo<RetCC_F128SoftFloat>>>,
  CCIfSubtargetNot<"useSoftFloat()",
      CCIfType<[i64], CCDelegateTo<RetCC_F128HardFloat>>>
]>;

//===----------------------------------------------------------------------===//
// Mips O32 Calling Convention
//===----------------------------------------------------------------------===//

def CC_MipsO32 : CallingConv<[
  // Promote i8/i16 arguments to i32.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // Integer values get stored in stack slots that are 4 bytes in
  // size and 4-byte aligned.
  CCIfType<[i32, f32], CCAssignToStack<4, 4>>,

  // Integer values get stored in stack slots that are 8 bytes in
  // size and 8-byte aligned.
  CCIfType<[f64], CCAssignToStack<8, 8>>
]>;

// Only the return rules are defined here for O32. The rules for argument
// passing are defined in MipsISelLowering.cpp.
def RetCC_MipsO32 : CallingConv<[
  // Promote i1/i8/i16 return values to i32.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // i32 are returned in registers V0, V1, A0, A1, unless the original return
  // type was a vector of floats.
  CCIfOrigArgWasNotVectorFloat<CCIfType<[i32],
                                        CCAssignToReg<[V0, V1, A0, A1]>>>,

  // f32 are returned in registers F0, F2
  CCIfType<[f32], CCAssignToReg<[F0, F2]>>,

  // f64 arguments are returned in D0_64 and D2_64 in FP64bit mode or
  // in D0 and D1 in FP32bit mode.
  CCIfType<[f64], CCIfSubtarget<"isFP64bit()", CCAssignToReg<[D0_64, D2_64]>>>,
  CCIfType<[f64], CCIfSubtargetNot<"isFP64bit()", CCAssignToReg<[D0, D1]>>>
]>;

def CC_MipsO32_FP32 : CustomCallingConv;
def CC_MipsO32_FP64 : CustomCallingConv;

def CC_MipsO32_FP : CallingConv<[
  CCIfSubtargetNot<"isFP64bit()", CCDelegateTo<CC_MipsO32_FP32>>,
  CCIfSubtarget<"isFP64bit()", CCDelegateTo<CC_MipsO32_FP64>>
]>;

//===----------------------------------------------------------------------===//
// Mips N32/64 Calling Convention
//===----------------------------------------------------------------------===//

def CC_MipsN_SoftFloat : CallingConv<[
  CCAssignToRegWithShadow<[A0, A1, A2, A3,
                           T0, T1, T2, T3],
                          [D12_64, D13_64, D14_64, D15_64,
                           D16_64, D17_64, D18_64, D19_64]>,
  CCAssignToStack<4, 8>
]>;

def CC_MipsN : CallingConv<[
  CCIfType<[i8, i16, i32, i64],
      CCIfSubtargetNot<"isLittle()",
          CCIfInReg<CCPromoteToUpperBitsInType<i64>>>>,

  // All integers (except soft-float integers) are promoted to 64-bit.
  CCIfType<[i8, i16, i32], CCIfOrigArgWasNotFloat<CCPromoteToType<i64>>>,

  // The only i32's we have left are soft-float arguments.
  CCIfSubtarget<"useSoftFloat()", CCIfType<[i32], CCDelegateTo<CC_MipsN_SoftFloat>>>,

  // Integer arguments are passed in integer registers.
  CCIfType<[i64], CCAssignToRegWithShadow<[A0_64, A1_64, A2_64, A3_64,
                                           T0_64, T1_64, T2_64, T3_64],
                                          [D12_64, D13_64, D14_64, D15_64,
                                           D16_64, D17_64, D18_64, D19_64]>>,

  // f32 arguments are passed in single precision FP registers.
  CCIfType<[f32], CCAssignToRegWithShadow<[F12, F13, F14, F15,
                                           F16, F17, F18, F19],
                                          [A0_64, A1_64, A2_64, A3_64,
                                           T0_64, T1_64, T2_64, T3_64]>>,

  // f64 arguments are passed in double precision FP registers.
  CCIfType<[f64], CCAssignToRegWithShadow<[D12_64, D13_64, D14_64, D15_64,
                                           D16_64, D17_64, D18_64, D19_64],
                                          [A0_64, A1_64, A2_64, A3_64,
                                           T0_64, T1_64, T2_64, T3_64]>>,

  // All stack parameter slots become 64-bit doublewords and are 8-byte aligned.
  CCIfType<[f32], CCAssignToStack<4, 8>>,
  CCIfType<[i64, f64], CCAssignToStack<8, 8>>
]>;

// N32/64 variable arguments.
// All arguments are passed in integer registers.
def CC_MipsN_VarArg : CallingConv<[
  CCIfType<[i8, i16, i32, i64],
      CCIfSubtargetNot<"isLittle()",
          CCIfInReg<CCPromoteToUpperBitsInType<i64>>>>,

  // All integers are promoted to 64-bit.
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,

  CCIfType<[f32], CCAssignToReg<[A0, A1, A2, A3, T0, T1, T2, T3]>>,

  CCIfType<[i64, f64], CCAssignToReg<[A0_64, A1_64, A2_64, A3_64,
                                      T0_64, T1_64, T2_64, T3_64]>>,

  // All stack parameter slots become 64-bit doublewords and are 8-byte aligned.
  CCIfType<[f32], CCAssignToStack<4, 8>>,
  CCIfType<[i64, f64], CCAssignToStack<8, 8>>
]>;

def RetCC_MipsN : CallingConv<[
  // f128 needs to be handled similarly to f32 and f64. However, f128 is not
  // legal and is lowered to i128 which is further lowered to a pair of i64's.
  // This presents us with a problem for the calling convention since hard-float
  // still needs to pass them in FPU registers, and soft-float needs to use $v0,
  // and $a0 instead of the usual $v0, and $v1. We therefore resort to a
  // pre-analyze (see PreAnalyzeReturnForF128()) step to pass information on
  // whether the result was originally an f128 into the tablegen-erated code.
  //
  // f128 should only occur for the N64 ABI where long double is 128-bit. On
  // N32, long double is equivalent to double.
  CCIfType<[i64], CCIfOrigArgWasF128<CCDelegateTo<RetCC_F128>>>,

  // Aggregate returns are positioned at the lowest address in the slot for
  // both little and big-endian targets. When passing in registers, this
  // requires that big-endian targets shift the value into the upper bits.
  CCIfSubtarget<"isLittle()",
      CCIfType<[i8, i16, i32, i64], CCIfInReg<CCPromoteToType<i64>>>>,
  CCIfSubtargetNot<"isLittle()",
      CCIfType<[i8, i16, i32, i64],
          CCIfInReg<CCPromoteToUpperBitsInType<i64>>>>,

  // i64 are returned in registers V0_64, V1_64
  CCIfType<[i64], CCAssignToReg<[V0_64, V1_64]>>,

  // f32 are returned in registers F0, F2
  CCIfType<[f32], CCAssignToReg<[F0, F2]>>,

  // f64 are returned in registers D0, D2
  CCIfType<[f64], CCAssignToReg<[D0_64, D2_64]>>
]>;

//===----------------------------------------------------------------------===//
// Mips FastCC Calling Convention
//===----------------------------------------------------------------------===//
def CC_MipsO32_FastCC : CallingConv<[
  // f64 arguments are passed in double-precision floating pointer registers.
  CCIfType<[f64], CCIfSubtargetNot<"isFP64bit()",
                                   CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6,
                                                  D7, D8, D9]>>>,
  CCIfType<[f64], CCIfSubtarget<"isFP64bit()", CCIfSubtarget<"useOddSPReg()",
                                CCAssignToReg<[D0_64, D1_64, D2_64, D3_64,
                                               D4_64, D5_64, D6_64, D7_64,
                                               D8_64, D9_64, D10_64, D11_64,
                                               D12_64, D13_64, D14_64, D15_64,
                                               D16_64, D17_64, D18_64,
                                               D19_64]>>>>,
  CCIfType<[f64], CCIfSubtarget<"isFP64bit()", CCIfSubtarget<"noOddSPReg()",
                                CCAssignToReg<[D0_64, D2_64, D4_64, D6_64,
                                               D8_64, D10_64, D12_64, D14_64,
                                               D16_64, D18_64]>>>>,

  // Stack parameter slots for f64 are 64-bit doublewords and 8-byte aligned.
  CCIfType<[f64], CCAssignToStack<8, 8>>
]>;

def CC_MipsN_FastCC : CallingConv<[
  // Integer arguments are passed in integer registers.
  CCIfType<[i64], CCAssignToReg<[A0_64, A1_64, A2_64, A3_64, T0_64, T1_64,
                                 T2_64, T3_64, T4_64, T5_64, T6_64, T7_64,
                                 T8_64, V1_64]>>,

  // f64 arguments are passed in double-precision floating pointer registers.
  CCIfType<[f64], CCAssignToReg<[D0_64, D1_64, D2_64, D3_64, D4_64, D5_64,
                                 D6_64, D7_64, D8_64, D9_64, D10_64, D11_64,
                                 D12_64, D13_64, D14_64, D15_64, D16_64, D17_64,
                                 D18_64, D19_64]>>,

  // Stack parameter slots for i64 and f64 are 64-bit doublewords and
  // 8-byte aligned.
  CCIfType<[i64, f64], CCAssignToStack<8, 8>>
]>;

def CC_Mips_FastCC : CallingConv<[
  // Handles byval parameters.
  CCIfByVal<CCPassByVal<4, 4>>,

  // Promote i8/i16 arguments to i32.
  CCIfType<[i8, i16], CCPromoteToType<i32>>,

  // Integer arguments are passed in integer registers. All scratch registers,
  // except for AT, V0 and T9, are available to be used as argument registers.
  CCIfType<[i32], CCIfSubtargetNot<"isTargetNaCl()",
      CCAssignToReg<[A0, A1, A2, A3, T0, T1, T2, T3, T4, T5, T6, T7, T8, V1]>>>,

  // In NaCl, T6, T7 and T8 are reserved and not available as argument
  // registers for fastcc.  T6 contains the mask for sandboxing control flow
  // (indirect jumps and calls).  T7 contains the mask for sandboxing memory
  // accesses (loads and stores).  T8 contains the thread pointer.
  CCIfType<[i32], CCIfSubtarget<"isTargetNaCl()",
      CCAssignToReg<[A0, A1, A2, A3, T0, T1, T2, T3, T4, T5, V1]>>>,

  // f32 arguments are passed in single-precision floating pointer registers.
  CCIfType<[f32], CCIfSubtarget<"useOddSPReg()",
      CCAssignToReg<[F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13,
                     F14, F15, F16, F17, F18, F19]>>>,

  // Don't use odd numbered single-precision registers for -mno-odd-spreg.
  CCIfType<[f32], CCIfSubtarget<"noOddSPReg()",
      CCAssignToReg<[F0, F2, F4, F6, F8, F10, F12, F14, F16, F18]>>>,

  // Stack parameter slots for i32 and f32 are 32-bit words and 4-byte aligned.
  CCIfType<[i32, f32], CCAssignToStack<4, 4>>,

  CCIfSubtarget<"isABI_O32()", CCDelegateTo<CC_MipsO32_FastCC>>,
  CCDelegateTo<CC_MipsN_FastCC>
]>;

//===----------------------------------------------------------------------===//
// Mips Calling Convention Dispatch
//===----------------------------------------------------------------------===//

def RetCC_Mips : CallingConv<[
  CCIfSubtarget<"isABI_N32()", CCDelegateTo<RetCC_MipsN>>,
  CCIfSubtarget<"isABI_N64()", CCDelegateTo<RetCC_MipsN>>,
  CCDelegateTo<RetCC_MipsO32>
]>;

def CC_Mips_ByVal : CallingConv<[
  CCIfSubtarget<"isABI_O32()", CCIfByVal<CCPassByVal<4, 4>>>,
  CCIfByVal<CCPassByVal<8, 8>>
]>;

def CC_Mips16RetHelper : CallingConv<[
  CCIfByVal<CCDelegateTo<CC_Mips_ByVal>>,

  // Integer arguments are passed in integer registers.
  CCIfType<[i32], CCAssignToReg<[V0, V1, A0, A1]>>
]>;

def CC_Mips_FixedArg : CallingConv<[
  // Mips16 needs special handling on some functions.
  CCIf<"State.getCallingConv() != CallingConv::Fast",
      CCIfSpecialCallingConv<"Mips16RetHelperConv",
           CCDelegateTo<CC_Mips16RetHelper>>>,

  CCIfByVal<CCDelegateTo<CC_Mips_ByVal>>,

  // f128 needs to be handled similarly to f32 and f64 on hard-float. However,
  // f128 is not legal and is lowered to i128 which is further lowered to a pair
  // of i64's.
  // This presents us with a problem for the calling convention since hard-float
  // still needs to pass them in FPU registers. We therefore resort to a
  // pre-analyze (see PreAnalyzeFormalArgsForF128()) step to pass information on
  // whether the argument was originally an f128 into the tablegen-erated code.
  //
  // f128 should only occur for the N64 ABI where long double is 128-bit. On
  // N32, long double is equivalent to double.
  CCIfType<[i64],
      CCIfSubtargetNot<"useSoftFloat()",
          CCIfOrigArgWasF128<CCBitConvertToType<f64>>>>,

  CCIfCC<"CallingConv::Fast", CCDelegateTo<CC_Mips_FastCC>>,

  CCIfSubtarget<"isABI_O32()", CCDelegateTo<CC_MipsO32_FP>>,
  CCDelegateTo<CC_MipsN>
]>;

def CC_Mips_VarArg : CallingConv<[
  CCIfByVal<CCDelegateTo<CC_Mips_ByVal>>,

  CCIfSubtarget<"isABI_O32()", CCDelegateTo<CC_MipsO32_FP>>,
  CCDelegateTo<CC_MipsN_VarArg>
]>;

def CC_Mips : CallingConv<[
  CCIfVarArg<CCIfArgIsVarArg<CCDelegateTo<CC_Mips_VarArg>>>,
  CCDelegateTo<CC_Mips_FixedArg>
]>;

//===----------------------------------------------------------------------===//
// Callee-saved register lists.
//===----------------------------------------------------------------------===//

def CSR_SingleFloatOnly : CalleeSavedRegs<(add (sequence "F%u", 31, 20), RA, FP,
                                               (sequence "S%u", 7, 0))>;

def CSR_O32_FPXX : CalleeSavedRegs<(add (sequence "D%u", 15, 10), RA, FP,
                                        (sequence "S%u", 7, 0))> {
  let OtherPreserved = (add (decimate (sequence "F%u", 30, 20), 2));
}

def CSR_O32 : CalleeSavedRegs<(add (sequence "D%u", 15, 10), RA, FP,
                                   (sequence "S%u", 7, 0))>;

def CSR_O32_FP64 :
  CalleeSavedRegs<(add (decimate (sequence "D%u_64", 30, 20), 2), RA, FP,
                       (sequence "S%u", 7, 0))>;

def CSR_N32 : CalleeSavedRegs<(add D20_64, D22_64, D24_64, D26_64, D28_64,
                                   D30_64, RA_64, FP_64, GP_64,
                                   (sequence "S%u_64", 7, 0))>;

def CSR_N64 : CalleeSavedRegs<(add (sequence "D%u_64", 31, 24), RA_64, FP_64,
                                   GP_64, (sequence "S%u_64", 7, 0))>;

def CSR_Mips16RetHelper :
  CalleeSavedRegs<(add V0, V1, FP,
                   (sequence "A%u", 3, 0), (sequence "S%u", 7, 0),
                   (sequence "D%u", 15, 10))>;

def CSR_Interrupt_32R6 : CalleeSavedRegs<(add (sequence "A%u", 3, 0),
                                              (sequence "S%u", 7, 0),
                                              (sequence "V%u", 1, 0),
                                              (sequence "T%u", 9, 0),
                                              RA, FP, GP, AT)>;

def CSR_Interrupt_32 : CalleeSavedRegs<(add (sequence "A%u", 3, 0),
                                            (sequence "S%u", 7, 0),
                                            (sequence "V%u", 1, 0),
                                            (sequence "T%u", 9, 0),
                                            RA, FP, GP, AT, LO0, HI0)>;

def CSR_Interrupt_64R6 : CalleeSavedRegs<(add (sequence "A%u_64", 3, 0),
                                              (sequence "V%u_64", 1, 0),
                                              (sequence "S%u_64", 7, 0),
                                              (sequence "T%u_64", 9, 0),
                                              RA_64, FP_64, GP_64, AT_64)>;

def CSR_Interrupt_64 : CalleeSavedRegs<(add (sequence "A%u_64", 3, 0),
                                            (sequence "S%u_64", 7, 0),
                                            (sequence "T%u_64", 9, 0),
                                            (sequence "V%u_64", 1, 0),
                                            RA_64, FP_64, GP_64, AT_64,
                                            LO0_64, HI0_64)>;
