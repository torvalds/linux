//===- PPCCallingConv.td - Calling Conventions for PowerPC -*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the PowerPC 32- and 64-bit
// architectures.
//
//===----------------------------------------------------------------------===//

/// CCIfSubtarget - Match if the current subtarget has a feature F.
class CCIfSubtarget<string F, CCAction A>
    : CCIf<!strconcat("static_cast<const PPCSubtarget&>"
                       "(State.getMachineFunction().getSubtarget()).",
                     F),
          A>;
class CCIfNotSubtarget<string F, CCAction A>
    : CCIf<!strconcat("!static_cast<const PPCSubtarget&>"
                       "(State.getMachineFunction().getSubtarget()).",
                     F),
          A>;
class CCIfOrigArgWasNotPPCF128<CCAction A>
    : CCIf<"!static_cast<PPCCCState *>(&State)->WasOriginalArgPPCF128(ValNo)",
           A>;
class CCIfOrigArgWasPPCF128<CCAction A>
    : CCIf<"static_cast<PPCCCState *>(&State)->WasOriginalArgPPCF128(ValNo)",
           A>;

//===----------------------------------------------------------------------===//
// Return Value Calling Convention
//===----------------------------------------------------------------------===//

// PPC64 AnyReg return-value convention. No explicit register is specified for
// the return-value. The register allocator is allowed and expected to choose
// any free register.
//
// This calling convention is currently only supported by the stackmap and
// patchpoint intrinsics. All other uses will result in an assert on Debug
// builds. On Release builds we fallback to the PPC C calling convention.
def RetCC_PPC64_AnyReg : CallingConv<[
  CCCustom<"CC_PPC_AnyReg_Error">
]>;

// Return-value convention for PowerPC coldcc.
def RetCC_PPC_Cold : CallingConv<[
  // Use the same return registers as RetCC_PPC, but limited to only
  // one return value. The remaining return values will be saved to
  // the stack.
  CCIfType<[i32, i1], CCIfSubtarget<"isPPC64()", CCPromoteToType<i64>>>,
  CCIfType<[i1], CCIfNotSubtarget<"isPPC64()", CCPromoteToType<i32>>>,

  CCIfType<[i32], CCAssignToReg<[R3]>>,
  CCIfType<[i64], CCAssignToReg<[X3]>>,
  CCIfType<[i128], CCAssignToReg<[X3]>>,

  CCIfType<[f32], CCAssignToReg<[F1]>>,
  CCIfType<[f64], CCAssignToReg<[F1]>>,
  CCIfType<[f128], CCIfSubtarget<"hasP9Vector()", CCAssignToReg<[V2]>>>,

  CCIfType<[v4f64, v4f32, v4i1],
           CCIfSubtarget<"hasQPX()", CCAssignToReg<[QF1]>>>,

  CCIfType<[v16i8, v8i16, v4i32, v2i64, v1i128, v4f32, v2f64],
           CCIfSubtarget<"hasAltivec()",
           CCAssignToReg<[V2]>>>
]>;

// Return-value convention for PowerPC
def RetCC_PPC : CallingConv<[
  CCIfCC<"CallingConv::AnyReg", CCDelegateTo<RetCC_PPC64_AnyReg>>,

  // On PPC64, integer return values are always promoted to i64
  CCIfType<[i32, i1], CCIfSubtarget<"isPPC64()", CCPromoteToType<i64>>>,
  CCIfType<[i1], CCIfNotSubtarget<"isPPC64()", CCPromoteToType<i32>>>,

  CCIfType<[i32], CCAssignToReg<[R3, R4, R5, R6, R7, R8, R9, R10]>>,
  CCIfType<[i64], CCAssignToReg<[X3, X4, X5, X6]>>,
  CCIfType<[i128], CCAssignToReg<[X3, X4, X5, X6]>>,

  // Floating point types returned as "direct" go into F1 .. F8; note that
  // only the ELFv2 ABI fully utilizes all these registers.
  CCIfNotSubtarget<"hasSPE()",
       CCIfType<[f32], CCAssignToReg<[F1, F2, F3, F4, F5, F6, F7, F8]>>>,
  CCIfNotSubtarget<"hasSPE()",
       CCIfType<[f64], CCAssignToReg<[F1, F2, F3, F4, F5, F6, F7, F8]>>>,
  CCIfSubtarget<"hasSPE()",
       CCIfType<[f32], CCAssignToReg<[R3, R4, R5, R6, R7, R8, R9, R10]>>>,
  CCIfSubtarget<"hasSPE()",
       CCIfType<[f64], CCAssignToReg<[S3, S4, S5, S6, S7, S8, S9, S10]>>>,

  // For P9, f128 are passed in vector registers.
  CCIfType<[f128],
           CCIfSubtarget<"hasP9Vector()",
           CCAssignToReg<[V2, V3, V4, V5, V6, V7, V8, V9]>>>,

  // QPX vectors are returned in QF1 and QF2. 
  CCIfType<[v4f64, v4f32, v4i1],
           CCIfSubtarget<"hasQPX()", CCAssignToReg<[QF1, QF2]>>>,
 
  // Vector types returned as "direct" go into V2 .. V9; note that only the
  // ELFv2 ABI fully utilizes all these registers.
  CCIfType<[v16i8, v8i16, v4i32, v2i64, v1i128, v4f32, v2f64],
           CCIfSubtarget<"hasAltivec()",
           CCAssignToReg<[V2, V3, V4, V5, V6, V7, V8, V9]>>>
]>;

// No explicit register is specified for the AnyReg calling convention. The
// register allocator may assign the arguments to any free register.
//
// This calling convention is currently only supported by the stackmap and
// patchpoint intrinsics. All other uses will result in an assert on Debug
// builds. On Release builds we fallback to the PPC C calling convention.
def CC_PPC64_AnyReg : CallingConv<[
  CCCustom<"CC_PPC_AnyReg_Error">
]>;

// Note that we don't currently have calling conventions for 64-bit
// PowerPC, but handle all the complexities of the ABI in the lowering
// logic.  FIXME: See if the logic can be simplified with use of CCs.
// This may require some extensions to current table generation.

// Simple calling convention for 64-bit ELF PowerPC fast isel.
// Only handle ints and floats.  All ints are promoted to i64.
// Vector types and quadword ints are not handled.
def CC_PPC64_ELF_FIS : CallingConv<[
  CCIfCC<"CallingConv::AnyReg", CCDelegateTo<CC_PPC64_AnyReg>>,

  CCIfType<[i1],  CCPromoteToType<i64>>,
  CCIfType<[i8],  CCPromoteToType<i64>>,
  CCIfType<[i16], CCPromoteToType<i64>>,
  CCIfType<[i32], CCPromoteToType<i64>>,
  CCIfType<[i64], CCAssignToReg<[X3, X4, X5, X6, X7, X8, X9, X10]>>,
  CCIfType<[f32, f64], CCAssignToReg<[F1, F2, F3, F4, F5, F6, F7, F8]>>
]>;

// Simple return-value convention for 64-bit ELF PowerPC fast isel.
// All small ints are promoted to i64.  Vector types, quadword ints,
// and multiple register returns are "supported" to avoid compile
// errors, but none are handled by the fast selector.
def RetCC_PPC64_ELF_FIS : CallingConv<[
  CCIfCC<"CallingConv::AnyReg", CCDelegateTo<RetCC_PPC64_AnyReg>>,

  CCIfType<[i1],   CCPromoteToType<i64>>,
  CCIfType<[i8],   CCPromoteToType<i64>>,
  CCIfType<[i16],  CCPromoteToType<i64>>,
  CCIfType<[i32],  CCPromoteToType<i64>>,
  CCIfType<[i64],  CCAssignToReg<[X3, X4, X5, X6]>>,
  CCIfType<[i128], CCAssignToReg<[X3, X4, X5, X6]>>,
  CCIfType<[f32],  CCAssignToReg<[F1, F2, F3, F4, F5, F6, F7, F8]>>,
  CCIfType<[f64],  CCAssignToReg<[F1, F2, F3, F4, F5, F6, F7, F8]>>,
  CCIfType<[f128],
           CCIfSubtarget<"hasP9Vector()",
           CCAssignToReg<[V2, V3, V4, V5, V6, V7, V8, V9]>>>,
  CCIfType<[v4f64, v4f32, v4i1],
           CCIfSubtarget<"hasQPX()", CCAssignToReg<[QF1, QF2]>>>,
  CCIfType<[v16i8, v8i16, v4i32, v2i64, v1i128, v4f32, v2f64],
           CCIfSubtarget<"hasAltivec()",
           CCAssignToReg<[V2, V3, V4, V5, V6, V7, V8, V9]>>>
]>;

//===----------------------------------------------------------------------===//
// PowerPC System V Release 4 32-bit ABI
//===----------------------------------------------------------------------===//

def CC_PPC32_SVR4_Common : CallingConv<[
  CCIfType<[i1], CCPromoteToType<i32>>,

  // The ABI requires i64 to be passed in two adjacent registers with the first
  // register having an odd register number.
  CCIfType<[i32],
  CCIfSplit<CCIfSubtarget<"useSoftFloat()", 
            CCIfOrigArgWasNotPPCF128<
            CCCustom<"CC_PPC32_SVR4_Custom_AlignArgRegs">>>>>,
  
  CCIfType<[i32],
  CCIfSplit<CCIfNotSubtarget<"useSoftFloat()", 
                            CCCustom<"CC_PPC32_SVR4_Custom_AlignArgRegs">>>>,
  CCIfSplit<CCIfSubtarget<"useSoftFloat()",
                          CCIfOrigArgWasPPCF128<CCCustom<
                          "CC_PPC32_SVR4_Custom_SkipLastArgRegsPPCF128">>>>,

  // The 'nest' parameter, if any, is passed in R11.
  CCIfNest<CCAssignToReg<[R11]>>,

  // The first 8 integer arguments are passed in integer registers.
  CCIfType<[i32], CCAssignToReg<[R3, R4, R5, R6, R7, R8, R9, R10]>>,

  // Make sure the i64 words from a long double are either both passed in
  // registers or both passed on the stack.
  CCIfType<[f64], CCIfSplit<CCCustom<"CC_PPC32_SVR4_Custom_AlignFPArgRegs">>>,
  
  // FP values are passed in F1 - F8.
  CCIfType<[f32, f64],
           CCIfNotSubtarget<"hasSPE()",
                            CCAssignToReg<[F1, F2, F3, F4, F5, F6, F7, F8]>>>,
  CCIfType<[f64],
           CCIfSubtarget<"hasSPE()",
                         CCAssignToReg<[S3, S4, S5, S6, S7, S8, S9, S10]>>>,
  CCIfType<[f32],
           CCIfSubtarget<"hasSPE()",
                         CCAssignToReg<[R3, R4, R5, R6, R7, R8, R9, R10]>>>,

  // Split arguments have an alignment of 8 bytes on the stack.
  CCIfType<[i32], CCIfSplit<CCAssignToStack<4, 8>>>,
  
  CCIfType<[i32], CCAssignToStack<4, 4>>,
  
  // Floats are stored in double precision format, thus they have the same
  // alignment and size as doubles.
  // With SPE floats are stored as single precision, so have alignment and
  // size of int.
  CCIfType<[f32,f64], CCIfNotSubtarget<"hasSPE()", CCAssignToStack<8, 8>>>,
  CCIfType<[f32], CCIfSubtarget<"hasSPE()", CCAssignToStack<4, 4>>>,
  CCIfType<[f64], CCIfSubtarget<"hasSPE()", CCAssignToStack<8, 8>>>,

  // QPX vectors that are stored in double precision need 32-byte alignment.
  CCIfType<[v4f64, v4i1], CCAssignToStack<32, 32>>,

  // Vectors and float128 get 16-byte stack slots that are 16-byte aligned.
  CCIfType<[v16i8, v8i16, v4i32, v4f32, v2f64, v2i64], CCAssignToStack<16, 16>>,
  CCIfType<[f128], CCIfSubtarget<"hasP9Vector()", CCAssignToStack<16, 16>>>
]>;

// This calling convention puts vector arguments always on the stack. It is used
// to assign vector arguments which belong to the variable portion of the
// parameter list of a variable argument function.
def CC_PPC32_SVR4_VarArg : CallingConv<[
  CCDelegateTo<CC_PPC32_SVR4_Common>
]>;

// In contrast to CC_PPC32_SVR4_VarArg, this calling convention first tries to
// put vector arguments in vector registers before putting them on the stack.
def CC_PPC32_SVR4 : CallingConv<[
  // QPX vectors mirror the scalar FP convention.
  CCIfType<[v4f64, v4f32, v4i1], CCIfSubtarget<"hasQPX()",
    CCAssignToReg<[QF1, QF2, QF3, QF4, QF5, QF6, QF7, QF8]>>>,

  // The first 12 Vector arguments are passed in AltiVec registers.
  CCIfType<[v16i8, v8i16, v4i32, v2i64, v1i128, v4f32, v2f64],
           CCIfSubtarget<"hasAltivec()", CCAssignToReg<[V2, V3, V4, V5, V6, V7,
                          V8, V9, V10, V11, V12, V13]>>>,

  // Float128 types treated as vector arguments.
  CCIfType<[f128],
           CCIfSubtarget<"hasP9Vector()", CCAssignToReg<[V2, V3, V4, V5, V6, V7,
                          V8, V9, V10, V11, V12, V13]>>>,
           
  CCDelegateTo<CC_PPC32_SVR4_Common>
]>;  

// Helper "calling convention" to handle aggregate by value arguments.
// Aggregate by value arguments are always placed in the local variable space
// of the caller. This calling convention is only used to assign those stack
// offsets in the callers stack frame.
//
// Still, the address of the aggregate copy in the callers stack frame is passed
// in a GPR (or in the parameter list area if all GPRs are allocated) from the
// caller to the callee. The location for the address argument is assigned by
// the CC_PPC32_SVR4 calling convention.
//
// The only purpose of CC_PPC32_SVR4_Custom_Dummy is to skip arguments which are
// not passed by value.
 
def CC_PPC32_SVR4_ByVal : CallingConv<[
  CCIfByVal<CCPassByVal<4, 4>>,
  
  CCCustom<"CC_PPC32_SVR4_Custom_Dummy">
]>;

def CSR_Altivec : CalleeSavedRegs<(add V20, V21, V22, V23, V24, V25, V26, V27,
                                       V28, V29, V30, V31)>;

def CSR_Darwin32 : CalleeSavedRegs<(add R13, R14, R15, R16, R17, R18, R19, R20,
                                        R21, R22, R23, R24, R25, R26, R27, R28,
                                        R29, R30, R31, F14, F15, F16, F17, F18,
                                        F19, F20, F21, F22, F23, F24, F25, F26,
                                        F27, F28, F29, F30, F31, CR2, CR3, CR4
                                   )>;

def CSR_Darwin32_Altivec : CalleeSavedRegs<(add CSR_Darwin32, CSR_Altivec)>;

// SPE does not use FPRs, so break out the common register set as base.
def CSR_SVR432_COMM : CalleeSavedRegs<(add R14, R15, R16, R17, R18, R19, R20,
                                          R21, R22, R23, R24, R25, R26, R27,
                                          R28, R29, R30, R31, CR2, CR3, CR4
                                      )>;
def CSR_SVR432 :  CalleeSavedRegs<(add CSR_SVR432_COMM, F14, F15, F16, F17, F18,
                                        F19, F20, F21, F22, F23, F24, F25, F26,
                                        F27, F28, F29, F30, F31
                                   )>;
def CSR_SPE : CalleeSavedRegs<(add S14, S15, S16, S17, S18, S19, S20, S21, S22,
                                   S23, S24, S25, S26, S27, S28, S29, S30, S31
                              )>;

def CSR_SVR432_Altivec : CalleeSavedRegs<(add CSR_SVR432, CSR_Altivec)>;

def CSR_SVR432_SPE : CalleeSavedRegs<(add CSR_SVR432_COMM, CSR_SPE)>;

def CSR_Darwin64 : CalleeSavedRegs<(add X13, X14, X15, X16, X17, X18, X19, X20,
                                        X21, X22, X23, X24, X25, X26, X27, X28,
                                        X29, X30, X31, F14, F15, F16, F17, F18,
                                        F19, F20, F21, F22, F23, F24, F25, F26,
                                        F27, F28, F29, F30, F31, CR2, CR3, CR4
                                   )>;

def CSR_Darwin64_Altivec : CalleeSavedRegs<(add CSR_Darwin64, CSR_Altivec)>;

def CSR_SVR464   : CalleeSavedRegs<(add X14, X15, X16, X17, X18, X19, X20,
                                        X21, X22, X23, X24, X25, X26, X27, X28,
                                        X29, X30, X31, F14, F15, F16, F17, F18,
                                        F19, F20, F21, F22, F23, F24, F25, F26,
                                        F27, F28, F29, F30, F31, CR2, CR3, CR4
                                   )>;

// CSRs that are handled by prologue, epilogue.
def CSR_SRV464_TLS_PE : CalleeSavedRegs<(add)>;

def CSR_SVR464_ViaCopy : CalleeSavedRegs<(add CSR_SVR464)>;

def CSR_SVR464_Altivec : CalleeSavedRegs<(add CSR_SVR464, CSR_Altivec)>;

def CSR_SVR464_Altivec_ViaCopy : CalleeSavedRegs<(add CSR_SVR464_Altivec)>;

def CSR_SVR464_R2 : CalleeSavedRegs<(add CSR_SVR464, X2)>;

def CSR_SVR464_R2_ViaCopy : CalleeSavedRegs<(add CSR_SVR464_R2)>;

def CSR_SVR464_R2_Altivec : CalleeSavedRegs<(add CSR_SVR464_Altivec, X2)>;

def CSR_SVR464_R2_Altivec_ViaCopy : CalleeSavedRegs<(add CSR_SVR464_R2_Altivec)>;

def CSR_NoRegs : CalleeSavedRegs<(add)>;

// coldcc calling convection marks most registers as non-volatile.
// Do not include r1 since the stack pointer is never considered a CSR.
// Do not include r2, since it is the TOC register and is added depending
// on whether or not the function uses the TOC and is a non-leaf.
// Do not include r0,r11,r13 as they are optional in functional linkage
// and value may be altered by inter-library calls.
// Do not include r12 as it is used as a scratch register.
// Do not include return registers r3, f1, v2.
def CSR_SVR32_ColdCC : CalleeSavedRegs<(add (sequence "R%u", 4, 10),
                                          (sequence "R%u", 14, 31),
                                          F0, (sequence "F%u", 2, 31),
                                          (sequence "CR%u", 0, 7))>;

def CSR_SVR32_ColdCC_Altivec : CalleeSavedRegs<(add CSR_SVR32_ColdCC,
                                            (sequence "V%u", 0, 1),
                                            (sequence "V%u", 3, 31))>;

def CSR_SVR64_ColdCC : CalleeSavedRegs<(add  (sequence "X%u", 4, 10),
                                             (sequence "X%u", 14, 31),
                                             F0, (sequence "F%u", 2, 31),
                                             (sequence "CR%u", 0, 7))>;

def CSR_SVR64_ColdCC_R2: CalleeSavedRegs<(add CSR_SVR64_ColdCC, X2)>;

def CSR_SVR64_ColdCC_Altivec : CalleeSavedRegs<(add CSR_SVR64_ColdCC,
                                             (sequence "V%u", 0, 1),
                                             (sequence "V%u", 3, 31))>;

def CSR_SVR64_ColdCC_R2_Altivec : CalleeSavedRegs<(add CSR_SVR64_ColdCC_Altivec, X2)>;

def CSR_64_AllRegs: CalleeSavedRegs<(add X0, (sequence "X%u", 3, 10),
                                             (sequence "X%u", 14, 31),
                                             (sequence "F%u", 0, 31),
                                             (sequence "CR%u", 0, 7))>;

def CSR_64_AllRegs_Altivec : CalleeSavedRegs<(add CSR_64_AllRegs,
                                             (sequence "V%u", 0, 31))>;

def CSR_64_AllRegs_VSX : CalleeSavedRegs<(add CSR_64_AllRegs_Altivec,
                                         (sequence "VSL%u", 0, 31))>;

