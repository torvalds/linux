//=- AArch64CallingConv.td - Calling Conventions for AArch64 -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for AArch64 architecture.
//
//===----------------------------------------------------------------------===//

/// CCIfAlign - Match of the original alignment of the arg
class CCIfAlign<string Align, CCAction A> :
  CCIf<!strconcat("ArgFlags.getOrigAlign() == ", Align), A>;
/// CCIfBigEndian - Match only if we're in big endian mode.
class CCIfBigEndian<CCAction A> :
  CCIf<"State.getMachineFunction().getDataLayout().isBigEndian()", A>;

//===----------------------------------------------------------------------===//
// ARM AAPCS64 Calling Convention
//===----------------------------------------------------------------------===//

def CC_AArch64_AAPCS : CallingConv<[
  CCIfType<[iPTR], CCBitConvertToType<i64>>,
  CCIfType<[v2f32], CCBitConvertToType<v2i32>>,
  CCIfType<[v2f64, v4f32], CCBitConvertToType<v2i64>>,

  // Big endian vectors must be passed as if they were 1-element vectors so that
  // their lanes are in a consistent order.
  CCIfBigEndian<CCIfType<[v2i32, v2f32, v4i16, v4f16, v8i8],
                         CCBitConvertToType<f64>>>,
  CCIfBigEndian<CCIfType<[v2i64, v2f64, v4i32, v4f32, v8i16, v8f16, v16i8],
                         CCBitConvertToType<f128>>>,

  // An SRet is passed in X8, not X0 like a normal pointer parameter.
  CCIfSRet<CCIfType<[i64], CCAssignToRegWithShadow<[X8], [W8]>>>,

  // Put ByVal arguments directly on the stack. Minimum size and alignment of a
  // slot is 64-bit.
  CCIfByVal<CCPassByVal<8, 8>>,

  // The 'nest' parameter, if any, is passed in X18.
  // Darwin uses X18 as the platform register and hence 'nest' isn't currently
  // supported there.
  CCIfNest<CCAssignToReg<[X18]>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i64], CCAssignToRegWithShadow<[X20], [W20]>>>,

  // A SwiftError is passed in X21.
  CCIfSwiftError<CCIfType<[i64], CCAssignToRegWithShadow<[X21], [W21]>>>,

  CCIfConsecutiveRegs<CCCustom<"CC_AArch64_Custom_Block">>,

  // Handle i1, i8, i16, i32, i64, f32, f64 and v2f64 by passing in registers,
  // up to eight each of GPR and FPR.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToRegWithShadow<[W0, W1, W2, W3, W4, W5, W6, W7],
                                          [X0, X1, X2, X3, X4, X5, X6, X7]>>,
  // i128 is split to two i64s, we can't fit half to register X7.
  CCIfType<[i64], CCIfSplit<CCAssignToRegWithShadow<[X0, X2, X4, X6],
                                                    [X0, X1, X3, X5]>>>,

  // i128 is split to two i64s, and its stack alignment is 16 bytes.
  CCIfType<[i64], CCIfSplit<CCAssignToStackWithShadow<8, 16, [X7]>>>,

  CCIfType<[i64], CCAssignToRegWithShadow<[X0, X1, X2, X3, X4, X5, X6, X7],
                                          [W0, W1, W2, W3, W4, W5, W6, W7]>>,
  CCIfType<[f16], CCAssignToRegWithShadow<[H0, H1, H2, H3, H4, H5, H6, H7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[f32], CCAssignToRegWithShadow<[S0, S1, S2, S3, S4, S5, S6, S7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[f64], CCAssignToRegWithShadow<[D0, D1, D2, D3, D4, D5, D6, D7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[v1i64, v2i32, v4i16, v8i8, v1f64, v2f32, v4f16],
           CCAssignToRegWithShadow<[D0, D1, D2, D3, D4, D5, D6, D7],
                                   [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[f128, v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16],
           CCAssignToReg<[Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,

  // If more than will fit in registers, pass them on the stack instead.
  CCIfType<[i1, i8, i16, f16], CCAssignToStack<8, 8>>,
  CCIfType<[i32, f32], CCAssignToStack<8, 8>>,
  CCIfType<[i64, f64, v1f64, v2f32, v1i64, v2i32, v4i16, v8i8, v4f16],
           CCAssignToStack<8, 8>>,
  CCIfType<[f128, v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16],
           CCAssignToStack<16, 16>>
]>;

def RetCC_AArch64_AAPCS : CallingConv<[
  CCIfType<[iPTR], CCBitConvertToType<i64>>,
  CCIfType<[v2f32], CCBitConvertToType<v2i32>>,
  CCIfType<[v2f64, v4f32], CCBitConvertToType<v2i64>>,

  CCIfSwiftError<CCIfType<[i64], CCAssignToRegWithShadow<[X21], [W21]>>>,

  // Big endian vectors must be passed as if they were 1-element vectors so that
  // their lanes are in a consistent order.
  CCIfBigEndian<CCIfType<[v2i32, v2f32, v4i16, v4f16, v8i8],
                         CCBitConvertToType<f64>>>,
  CCIfBigEndian<CCIfType<[v2i64, v2f64, v4i32, v4f32, v8i16, v8f16, v16i8],
                         CCBitConvertToType<f128>>>,

  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToRegWithShadow<[W0, W1, W2, W3, W4, W5, W6, W7],
                                          [X0, X1, X2, X3, X4, X5, X6, X7]>>,
  CCIfType<[i64], CCAssignToRegWithShadow<[X0, X1, X2, X3, X4, X5, X6, X7],
                                          [W0, W1, W2, W3, W4, W5, W6, W7]>>,
  CCIfType<[f16], CCAssignToRegWithShadow<[H0, H1, H2, H3, H4, H5, H6, H7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[f32], CCAssignToRegWithShadow<[S0, S1, S2, S3, S4, S5, S6, S7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[f64], CCAssignToRegWithShadow<[D0, D1, D2, D3, D4, D5, D6, D7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[v1i64, v2i32, v4i16, v8i8, v1f64, v2f32, v4f16],
      CCAssignToRegWithShadow<[D0, D1, D2, D3, D4, D5, D6, D7],
                              [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[f128, v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16],
      CCAssignToReg<[Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>
]>;

// Vararg functions on windows pass floats in integer registers
def CC_AArch64_Win64_VarArg : CallingConv<[
  CCIfType<[f16, f32], CCPromoteToType<f64>>,
  CCIfType<[f64], CCBitConvertToType<i64>>,
  CCDelegateTo<CC_AArch64_AAPCS>
]>;


// Darwin uses a calling convention which differs in only two ways
// from the standard one at this level:
//     + i128s (i.e. split i64s) don't need even registers.
//     + Stack slots are sized as needed rather than being at least 64-bit.
def CC_AArch64_DarwinPCS : CallingConv<[
  CCIfType<[iPTR], CCBitConvertToType<i64>>,
  CCIfType<[v2f32], CCBitConvertToType<v2i32>>,
  CCIfType<[v2f64, v4f32, f128], CCBitConvertToType<v2i64>>,

  // An SRet is passed in X8, not X0 like a normal pointer parameter.
  CCIfSRet<CCIfType<[i64], CCAssignToRegWithShadow<[X8], [W8]>>>,

  // Put ByVal arguments directly on the stack. Minimum size and alignment of a
  // slot is 64-bit.
  CCIfByVal<CCPassByVal<8, 8>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i64], CCAssignToRegWithShadow<[X20], [W20]>>>,

  // A SwiftError is passed in X21.
  CCIfSwiftError<CCIfType<[i64], CCAssignToRegWithShadow<[X21], [W21]>>>,

  CCIfConsecutiveRegs<CCCustom<"CC_AArch64_Custom_Block">>,

  // Handle i1, i8, i16, i32, i64, f32, f64 and v2f64 by passing in registers,
  // up to eight each of GPR and FPR.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToRegWithShadow<[W0, W1, W2, W3, W4, W5, W6, W7],
                                          [X0, X1, X2, X3, X4, X5, X6, X7]>>,
  // i128 is split to two i64s, we can't fit half to register X7.
  CCIfType<[i64],
           CCIfSplit<CCAssignToRegWithShadow<[X0, X1, X2, X3, X4, X5, X6],
                                             [W0, W1, W2, W3, W4, W5, W6]>>>,
  // i128 is split to two i64s, and its stack alignment is 16 bytes.
  CCIfType<[i64], CCIfSplit<CCAssignToStackWithShadow<8, 16, [X7]>>>,

  CCIfType<[i64], CCAssignToRegWithShadow<[X0, X1, X2, X3, X4, X5, X6, X7],
                                          [W0, W1, W2, W3, W4, W5, W6, W7]>>,
  CCIfType<[f16], CCAssignToRegWithShadow<[H0, H1, H2, H3, H4, H5, H6, H7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[f32], CCAssignToRegWithShadow<[S0, S1, S2, S3, S4, S5, S6, S7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[f64], CCAssignToRegWithShadow<[D0, D1, D2, D3, D4, D5, D6, D7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[v1i64, v2i32, v4i16, v8i8, v1f64, v2f32, v4f16],
           CCAssignToRegWithShadow<[D0, D1, D2, D3, D4, D5, D6, D7],
                                   [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16],
           CCAssignToReg<[Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,

  // If more than will fit in registers, pass them on the stack instead.
  CCIf<"ValVT == MVT::i1 || ValVT == MVT::i8", CCAssignToStack<1, 1>>,
  CCIf<"ValVT == MVT::i16 || ValVT == MVT::f16", CCAssignToStack<2, 2>>,
  CCIfType<[i32, f32], CCAssignToStack<4, 4>>,
  CCIfType<[i64, f64, v1f64, v2f32, v1i64, v2i32, v4i16, v8i8, v4f16],
           CCAssignToStack<8, 8>>,
  CCIfType<[v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16],
           CCAssignToStack<16, 16>>
]>;

def CC_AArch64_DarwinPCS_VarArg : CallingConv<[
  CCIfType<[iPTR], CCBitConvertToType<i64>>,
  CCIfType<[v2f32], CCBitConvertToType<v2i32>>,
  CCIfType<[v2f64, v4f32, f128], CCBitConvertToType<v2i64>>,

  CCIfConsecutiveRegs<CCCustom<"CC_AArch64_Custom_Stack_Block">>,

  // Handle all scalar types as either i64 or f64.
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,
  CCIfType<[f16, f32],     CCPromoteToType<f64>>,

  // Everything is on the stack.
  // i128 is split to two i64s, and its stack alignment is 16 bytes.
  CCIfType<[i64], CCIfSplit<CCAssignToStack<8, 16>>>,
  CCIfType<[i64, f64, v1i64, v2i32, v4i16, v8i8, v1f64, v2f32, v4f16],
           CCAssignToStack<8, 8>>,
  CCIfType<[v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16],
           CCAssignToStack<16, 16>>
]>;

// The WebKit_JS calling convention only passes the first argument (the callee)
// in register and the remaining arguments on stack. We allow 32bit stack slots,
// so that WebKit can write partial values in the stack and define the other
// 32bit quantity as undef.
def CC_AArch64_WebKit_JS : CallingConv<[
  // Handle i1, i8, i16, i32, and i64 passing in register X0 (W0).
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToRegWithShadow<[W0], [X0]>>,
  CCIfType<[i64], CCAssignToRegWithShadow<[X0], [W0]>>,

  // Pass the remaining arguments on the stack instead.
  CCIfType<[i32, f32], CCAssignToStack<4, 4>>,
  CCIfType<[i64, f64], CCAssignToStack<8, 8>>
]>;

def RetCC_AArch64_WebKit_JS : CallingConv<[
  CCIfType<[i32], CCAssignToRegWithShadow<[W0, W1, W2, W3, W4, W5, W6, W7],
                                          [X0, X1, X2, X3, X4, X5, X6, X7]>>,
  CCIfType<[i64], CCAssignToRegWithShadow<[X0, X1, X2, X3, X4, X5, X6, X7],
                                          [W0, W1, W2, W3, W4, W5, W6, W7]>>,
  CCIfType<[f32], CCAssignToRegWithShadow<[S0, S1, S2, S3, S4, S5, S6, S7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,
  CCIfType<[f64], CCAssignToRegWithShadow<[D0, D1, D2, D3, D4, D5, D6, D7],
                                          [Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>
]>;

//===----------------------------------------------------------------------===//
// ARM64 Calling Convention for GHC
//===----------------------------------------------------------------------===//

// This calling convention is specific to the Glasgow Haskell Compiler.
// The only documentation is the GHC source code, specifically the C header
// file:
//
//     https://github.com/ghc/ghc/blob/master/includes/stg/MachRegs.h
//
// which defines the registers for the Spineless Tagless G-Machine (STG) that
// GHC uses to implement lazy evaluation. The generic STG machine has a set of
// registers which are mapped to appropriate set of architecture specific
// registers for each CPU architecture.
//
// The STG Machine is documented here:
//
//    https://ghc.haskell.org/trac/ghc/wiki/Commentary/Compiler/GeneratedCode
//
// The AArch64 register mapping is under the heading "The ARMv8/AArch64 ABI
// register mapping".

def CC_AArch64_GHC : CallingConv<[
  CCIfType<[iPTR], CCBitConvertToType<i64>>,

  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v16i8, v4f32, f128], CCBitConvertToType<v2f64>>,

  CCIfType<[v2f64], CCAssignToReg<[Q4, Q5]>>,
  CCIfType<[f32], CCAssignToReg<[S8, S9, S10, S11]>>,
  CCIfType<[f64], CCAssignToReg<[D12, D13, D14, D15]>>,

  // Promote i8/i16/i32 arguments to i64.
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,

  // Pass in STG registers: Base, Sp, Hp, R1, R2, R3, R4, R5, R6, SpLim
  CCIfType<[i64], CCAssignToReg<[X19, X20, X21, X22, X23, X24, X25, X26, X27, X28]>>
]>;

// FIXME: LR is only callee-saved in the sense that *we* preserve it and are
// presumably a callee to someone. External functions may not do so, but this
// is currently safe since BL has LR as an implicit-def and what happens after a
// tail call doesn't matter.
//
// It would be better to model its preservation semantics properly (create a
// vreg on entry, use it in RET & tail call generation; make that vreg def if we
// end up saving LR as part of a call frame). Watch this space...
def CSR_AArch64_AAPCS : CalleeSavedRegs<(add LR, FP, X19, X20, X21, X22,
                                           X23, X24, X25, X26, X27, X28,
                                           D8,  D9,  D10, D11,
                                           D12, D13, D14, D15)>;

// Win64 has unwinding codes for an (FP,LR) pair, save_fplr and save_fplr_x.
// We put FP before LR, so that frame lowering logic generates (FP,LR) pairs,
// and not (LR,FP) pairs.
def CSR_Win_AArch64_AAPCS : CalleeSavedRegs<(add FP, LR, X19, X20, X21, X22,
                                               X23, X24, X25, X26, X27, X28,
                                               D8, D9, D10, D11,
                                               D12, D13, D14, D15)>;

// AArch64 PCS for vector functions (VPCS)
// must (additionally) preserve full Q8-Q23 registers
def CSR_AArch64_AAVPCS : CalleeSavedRegs<(add LR, FP, X19, X20, X21, X22,
                                          X23, X24, X25, X26, X27, X28,
                                          (sequence "Q%u", 8, 23))>;

// Constructors and destructors return 'this' in the iOS 64-bit C++ ABI; since
// 'this' and the pointer return value are both passed in X0 in these cases,
// this can be partially modelled by treating X0 as a callee-saved register;
// only the resulting RegMask is used; the SaveList is ignored
//
// (For generic ARM 64-bit ABI code, clang will not generate constructors or
// destructors with 'this' returns, so this RegMask will not be used in that
// case)
def CSR_AArch64_AAPCS_ThisReturn : CalleeSavedRegs<(add CSR_AArch64_AAPCS, X0)>;

def CSR_AArch64_AAPCS_SwiftError
    : CalleeSavedRegs<(sub CSR_AArch64_AAPCS, X21)>;

// The function used by Darwin to obtain the address of a thread-local variable
// guarantees more than a normal AAPCS function. x16 and x17 are used on the
// fast path for calculation, but other registers except X0 (argument/return)
// and LR (it is a call, after all) are preserved.
def CSR_AArch64_TLS_Darwin
    : CalleeSavedRegs<(add (sub (sequence "X%u", 1, 28), X16, X17),
                           FP,
                           (sequence "Q%u", 0, 31))>;

// We can only handle a register pair with adjacent registers, the register pair
// should belong to the same class as well. Since the access function on the
// fast path calls a function that follows CSR_AArch64_TLS_Darwin,
// CSR_AArch64_CXX_TLS_Darwin should be a subset of CSR_AArch64_TLS_Darwin.
def CSR_AArch64_CXX_TLS_Darwin
    : CalleeSavedRegs<(add CSR_AArch64_AAPCS,
                           (sub (sequence "X%u", 1, 28), X15, X16, X17, X18),
                           (sequence "D%u", 0, 31))>;

// CSRs that are handled by prologue, epilogue.
def CSR_AArch64_CXX_TLS_Darwin_PE
    : CalleeSavedRegs<(add LR, FP)>;

// CSRs that are handled explicitly via copies.
def CSR_AArch64_CXX_TLS_Darwin_ViaCopy
    : CalleeSavedRegs<(sub CSR_AArch64_CXX_TLS_Darwin, LR, FP)>;

// The ELF stub used for TLS-descriptor access saves every feasible
// register. Only X0 and LR are clobbered.
def CSR_AArch64_TLS_ELF
    : CalleeSavedRegs<(add (sequence "X%u", 1, 28), FP,
                           (sequence "Q%u", 0, 31))>;

def CSR_AArch64_AllRegs
    : CalleeSavedRegs<(add (sequence "W%u", 0, 30), WSP,
                           (sequence "X%u", 0, 28), FP, LR, SP,
                           (sequence "B%u", 0, 31), (sequence "H%u", 0, 31),
                           (sequence "S%u", 0, 31), (sequence "D%u", 0, 31),
                           (sequence "Q%u", 0, 31))>;

def CSR_AArch64_NoRegs : CalleeSavedRegs<(add)>;

def CSR_AArch64_RT_MostRegs :  CalleeSavedRegs<(add CSR_AArch64_AAPCS,
                                                (sequence "X%u", 9, 15))>;

def CSR_AArch64_StackProbe_Windows
    : CalleeSavedRegs<(add (sequence "X%u", 0, 15),
                           (sequence "X%u", 18, 28), FP, SP,
                           (sequence "Q%u", 0, 31))>;

// Variants of the standard calling conventions for shadow call stack.
// These all preserve x18 in addition to any other registers.
def CSR_AArch64_NoRegs_SCS
    : CalleeSavedRegs<(add CSR_AArch64_NoRegs, X18)>;
def CSR_AArch64_AllRegs_SCS
    : CalleeSavedRegs<(add CSR_AArch64_AllRegs, X18)>;
def CSR_AArch64_CXX_TLS_Darwin_SCS
    : CalleeSavedRegs<(add CSR_AArch64_CXX_TLS_Darwin, X18)>;
def CSR_AArch64_AAPCS_SwiftError_SCS
    : CalleeSavedRegs<(add CSR_AArch64_AAPCS_SwiftError, X18)>;
def CSR_AArch64_RT_MostRegs_SCS
    : CalleeSavedRegs<(add CSR_AArch64_RT_MostRegs, X18)>;
def CSR_AArch64_AAVPCS_SCS
    : CalleeSavedRegs<(add CSR_AArch64_AAVPCS, X18)>;
def CSR_AArch64_AAPCS_SCS
    : CalleeSavedRegs<(add CSR_AArch64_AAPCS, X18)>;
