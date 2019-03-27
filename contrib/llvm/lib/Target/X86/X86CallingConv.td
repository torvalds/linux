//===-- X86CallingConv.td - Calling Conventions X86 32/64 --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the X86-32 and X86-64
// architectures.
//
//===----------------------------------------------------------------------===//

/// CCIfSubtarget - Match if the current subtarget has a feature F.
class CCIfSubtarget<string F, CCAction A>
    : CCIf<!strconcat("static_cast<const X86Subtarget&>"
                       "(State.getMachineFunction().getSubtarget()).", F),
           A>;

/// CCIfNotSubtarget - Match if the current subtarget doesn't has a feature F.
class CCIfNotSubtarget<string F, CCAction A>
    : CCIf<!strconcat("!static_cast<const X86Subtarget&>"
                       "(State.getMachineFunction().getSubtarget()).", F),
           A>;

// Register classes for RegCall
class RC_X86_RegCall {
  list<Register> GPR_8 = [];
  list<Register> GPR_16 = [];
  list<Register> GPR_32 = [];
  list<Register> GPR_64 = [];
  list<Register> FP_CALL = [FP0];
  list<Register> FP_RET = [FP0, FP1];
  list<Register> XMM = [];
  list<Register> YMM = [];
  list<Register> ZMM = [];
}

// RegCall register classes for 32 bits
def RC_X86_32_RegCall : RC_X86_RegCall {
  let GPR_8 = [AL, CL, DL, DIL, SIL];
  let GPR_16 = [AX, CX, DX, DI, SI];
  let GPR_32 = [EAX, ECX, EDX, EDI, ESI];
  let GPR_64 = [RAX]; ///< Not actually used, but AssignToReg can't handle []
                      ///< \todo Fix AssignToReg to enable empty lists
  let XMM = [XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7];
  let YMM = [YMM0, YMM1, YMM2, YMM3, YMM4, YMM5, YMM6, YMM7];
  let ZMM = [ZMM0, ZMM1, ZMM2, ZMM3, ZMM4, ZMM5, ZMM6, ZMM7];
}

class RC_X86_64_RegCall : RC_X86_RegCall {
  let XMM = [XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
             XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15];
  let YMM = [YMM0, YMM1, YMM2, YMM3, YMM4, YMM5, YMM6, YMM7,
             YMM8, YMM9, YMM10, YMM11, YMM12, YMM13, YMM14, YMM15];
  let ZMM = [ZMM0, ZMM1, ZMM2, ZMM3, ZMM4, ZMM5, ZMM6, ZMM7,
             ZMM8, ZMM9, ZMM10, ZMM11, ZMM12, ZMM13, ZMM14, ZMM15];
}

def RC_X86_64_RegCall_Win : RC_X86_64_RegCall {
  let GPR_8 = [AL, CL, DL, DIL, SIL, R8B, R9B, R10B, R11B, R12B, R14B, R15B];
  let GPR_16 = [AX, CX, DX, DI, SI, R8W, R9W, R10W, R11W, R12W, R14W, R15W];
  let GPR_32 = [EAX, ECX, EDX, EDI, ESI, R8D, R9D, R10D, R11D, R12D, R14D, R15D];
  let GPR_64 = [RAX, RCX, RDX, RDI, RSI, R8, R9, R10, R11, R12, R14, R15];
}

def RC_X86_64_RegCall_SysV : RC_X86_64_RegCall {
  let GPR_8 = [AL, CL, DL, DIL, SIL, R8B, R9B, R12B, R13B, R14B, R15B];
  let GPR_16 = [AX, CX, DX, DI, SI, R8W, R9W, R12W, R13W, R14W, R15W];
  let GPR_32 = [EAX, ECX, EDX, EDI, ESI, R8D, R9D, R12D, R13D, R14D, R15D];
  let GPR_64 = [RAX, RCX, RDX, RDI, RSI, R8, R9, R12, R13, R14, R15];
}

// X86-64 Intel regcall calling convention.
multiclass X86_RegCall_base<RC_X86_RegCall RC> {
def CC_#NAME : CallingConv<[
  // Handles byval parameters.
    CCIfSubtarget<"is64Bit()", CCIfByVal<CCPassByVal<8, 8>>>,
    CCIfByVal<CCPassByVal<4, 4>>,

    // Promote i1/i8/i16/v1i1 arguments to i32.
    CCIfType<[i1, i8, i16, v1i1], CCPromoteToType<i32>>,

    // Promote v8i1/v16i1/v32i1 arguments to i32.
    CCIfType<[v8i1, v16i1, v32i1], CCPromoteToType<i32>>,

    // bool, char, int, enum, long, pointer --> GPR
    CCIfType<[i32], CCAssignToReg<RC.GPR_32>>,

    // long long, __int64 --> GPR
    CCIfType<[i64], CCAssignToReg<RC.GPR_64>>,

    // __mmask64 (v64i1) --> GPR64 (for x64) or 2 x GPR32 (for IA32)
    CCIfType<[v64i1], CCPromoteToType<i64>>,
    CCIfSubtarget<"is64Bit()", CCIfType<[i64], 
      CCAssignToReg<RC.GPR_64>>>,
    CCIfSubtarget<"is32Bit()", CCIfType<[i64], 
      CCCustom<"CC_X86_32_RegCall_Assign2Regs">>>,

    // float, double, float128 --> XMM
    // In the case of SSE disabled --> save to stack
    CCIfType<[f32, f64, f128], 
      CCIfSubtarget<"hasSSE1()", CCAssignToReg<RC.XMM>>>,

    // long double --> FP
    CCIfType<[f80], CCAssignToReg<RC.FP_CALL>>,

    // __m128, __m128i, __m128d --> XMM
    // In the case of SSE disabled --> save to stack
    CCIfType<[v16i8, v8i16, v4i32, v2i64, v4f32, v2f64], 
      CCIfSubtarget<"hasSSE1()", CCAssignToReg<RC.XMM>>>,

    // __m256, __m256i, __m256d --> YMM
    // In the case of SSE disabled --> save to stack
    CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64], 
      CCIfSubtarget<"hasAVX()", CCAssignToReg<RC.YMM>>>,

    // __m512, __m512i, __m512d --> ZMM
    // In the case of SSE disabled --> save to stack
    CCIfType<[v64i8, v32i16, v16i32, v8i64, v16f32, v8f64], 
      CCIfSubtarget<"hasAVX512()",CCAssignToReg<RC.ZMM>>>,

    // If no register was found -> assign to stack

    // In 64 bit, assign 64/32 bit values to 8 byte stack
    CCIfSubtarget<"is64Bit()", CCIfType<[i32, i64, f32, f64], 
      CCAssignToStack<8, 8>>>,

    // In 32 bit, assign 64/32 bit values to 8/4 byte stack
    CCIfType<[i32, f32], CCAssignToStack<4, 4>>,
    CCIfType<[i64, f64], CCAssignToStack<8, 4>>,

    // MMX type gets 8 byte slot in stack , while alignment depends on target
    CCIfSubtarget<"is64Bit()", CCIfType<[x86mmx], CCAssignToStack<8, 8>>>,
    CCIfType<[x86mmx], CCAssignToStack<8, 4>>,

    // float 128 get stack slots whose size and alignment depends 
    // on the subtarget.
    CCIfType<[f80, f128], CCAssignToStack<0, 0>>,

    // Vectors get 16-byte stack slots that are 16-byte aligned.
    CCIfType<[v16i8, v8i16, v4i32, v2i64, v4f32, v2f64], 
      CCAssignToStack<16, 16>>,

    // 256-bit vectors get 32-byte stack slots that are 32-byte aligned.
    CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64], 
      CCAssignToStack<32, 32>>,

    // 512-bit vectors get 64-byte stack slots that are 64-byte aligned.
    CCIfType<[v16i32, v8i64, v16f32, v8f64], CCAssignToStack<64, 64>>
]>;

def RetCC_#NAME : CallingConv<[
    // Promote i1, v1i1, v8i1 arguments to i8.
    CCIfType<[i1, v1i1, v8i1], CCPromoteToType<i8>>,

    // Promote v16i1 arguments to i16.
    CCIfType<[v16i1], CCPromoteToType<i16>>,

    // Promote v32i1 arguments to i32.
    CCIfType<[v32i1], CCPromoteToType<i32>>,

    // bool, char, int, enum, long, pointer --> GPR
    CCIfType<[i8], CCAssignToReg<RC.GPR_8>>,
    CCIfType<[i16], CCAssignToReg<RC.GPR_16>>,
    CCIfType<[i32], CCAssignToReg<RC.GPR_32>>,

    // long long, __int64 --> GPR
    CCIfType<[i64], CCAssignToReg<RC.GPR_64>>,

    // __mmask64 (v64i1) --> GPR64 (for x64) or 2 x GPR32 (for IA32)
    CCIfType<[v64i1], CCPromoteToType<i64>>,
    CCIfSubtarget<"is64Bit()", CCIfType<[i64], 
      CCAssignToReg<RC.GPR_64>>>,
    CCIfSubtarget<"is32Bit()", CCIfType<[i64], 
      CCCustom<"CC_X86_32_RegCall_Assign2Regs">>>,

    // long double --> FP
    CCIfType<[f80], CCAssignToReg<RC.FP_RET>>,

    // float, double, float128 --> XMM
    CCIfType<[f32, f64, f128], 
      CCIfSubtarget<"hasSSE1()", CCAssignToReg<RC.XMM>>>,

    // __m128, __m128i, __m128d --> XMM
    CCIfType<[v16i8, v8i16, v4i32, v2i64, v4f32, v2f64], 
      CCIfSubtarget<"hasSSE1()", CCAssignToReg<RC.XMM>>>,

    // __m256, __m256i, __m256d --> YMM
    CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64], 
      CCIfSubtarget<"hasAVX()", CCAssignToReg<RC.YMM>>>,

    // __m512, __m512i, __m512d --> ZMM
    CCIfType<[v64i8, v32i16, v16i32, v8i64, v16f32, v8f64], 
      CCIfSubtarget<"hasAVX512()", CCAssignToReg<RC.ZMM>>>
]>;
}

//===----------------------------------------------------------------------===//
// Return Value Calling Conventions
//===----------------------------------------------------------------------===//

// Return-value conventions common to all X86 CC's.
def RetCC_X86Common : CallingConv<[
  // Scalar values are returned in AX first, then DX.  For i8, the ABI
  // requires the values to be in AL and AH, however this code uses AL and DL
  // instead. This is because using AH for the second register conflicts with
  // the way LLVM does multiple return values -- a return of {i16,i8} would end
  // up in AX and AH, which overlap. Front-ends wishing to conform to the ABI
  // for functions that return two i8 values are currently expected to pack the
  // values into an i16 (which uses AX, and thus AL:AH).
  //
  // For code that doesn't care about the ABI, we allow returning more than two
  // integer values in registers.
  CCIfType<[v1i1],  CCPromoteToType<i8>>,
  CCIfType<[i1],  CCPromoteToType<i8>>,
  CCIfType<[i8] , CCAssignToReg<[AL, DL, CL]>>,
  CCIfType<[i16], CCAssignToReg<[AX, DX, CX]>>,
  CCIfType<[i32], CCAssignToReg<[EAX, EDX, ECX]>>,
  CCIfType<[i64], CCAssignToReg<[RAX, RDX, RCX]>>,

  // Boolean vectors of AVX-512 are returned in SIMD registers.
  // The call from AVX to AVX-512 function should work,
  // since the boolean types in AVX/AVX2 are promoted by default.
  CCIfType<[v2i1],  CCPromoteToType<v2i64>>,
  CCIfType<[v4i1],  CCPromoteToType<v4i32>>,
  CCIfType<[v8i1],  CCPromoteToType<v8i16>>,
  CCIfType<[v16i1], CCPromoteToType<v16i8>>,
  CCIfType<[v32i1], CCPromoteToType<v32i8>>,
  CCIfType<[v64i1], CCPromoteToType<v64i8>>,

  // Vector types are returned in XMM0 and XMM1, when they fit.  XMM2 and XMM3
  // can only be used by ABI non-compliant code. If the target doesn't have XMM
  // registers, it won't have vector types.
  CCIfType<[v16i8, v8i16, v4i32, v2i64, v4f32, v2f64],
            CCAssignToReg<[XMM0,XMM1,XMM2,XMM3]>>,

  // 256-bit vectors are returned in YMM0 and XMM1, when they fit. YMM2 and YMM3
  // can only be used by ABI non-compliant code. This vector type is only
  // supported while using the AVX target feature.
  CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64],
            CCAssignToReg<[YMM0,YMM1,YMM2,YMM3]>>,

  // 512-bit vectors are returned in ZMM0 and ZMM1, when they fit. ZMM2 and ZMM3
  // can only be used by ABI non-compliant code. This vector type is only
  // supported while using the AVX-512 target feature.
  CCIfType<[v64i8, v32i16, v16i32, v8i64, v16f32, v8f64],
            CCAssignToReg<[ZMM0,ZMM1,ZMM2,ZMM3]>>,

  // MMX vector types are always returned in MM0. If the target doesn't have
  // MM0, it doesn't support these vector types.
  CCIfType<[x86mmx], CCAssignToReg<[MM0]>>,

  // Long double types are always returned in FP0 (even with SSE),
  // except on Win64.
  CCIfNotSubtarget<"isTargetWin64()", CCIfType<[f80], CCAssignToReg<[FP0, FP1]>>>
]>;

// X86-32 C return-value convention.
def RetCC_X86_32_C : CallingConv<[
  // The X86-32 calling convention returns FP values in FP0, unless marked
  // with "inreg" (used here to distinguish one kind of reg from another,
  // weirdly; this is really the sse-regparm calling convention) in which
  // case they use XMM0, otherwise it is the same as the common X86 calling
  // conv.
  CCIfInReg<CCIfSubtarget<"hasSSE2()",
    CCIfType<[f32, f64], CCAssignToReg<[XMM0,XMM1,XMM2]>>>>,
  CCIfType<[f32,f64], CCAssignToReg<[FP0, FP1]>>,
  CCDelegateTo<RetCC_X86Common>
]>;

// X86-32 FastCC return-value convention.
def RetCC_X86_32_Fast : CallingConv<[
  // The X86-32 fastcc returns 1, 2, or 3 FP values in XMM0-2 if the target has
  // SSE2.
  // This can happen when a float, 2 x float, or 3 x float vector is split by
  // target lowering, and is returned in 1-3 sse regs.
  CCIfType<[f32], CCIfSubtarget<"hasSSE2()", CCAssignToReg<[XMM0,XMM1,XMM2]>>>,
  CCIfType<[f64], CCIfSubtarget<"hasSSE2()", CCAssignToReg<[XMM0,XMM1,XMM2]>>>,

  // For integers, ECX can be used as an extra return register
  CCIfType<[i8],  CCAssignToReg<[AL, DL, CL]>>,
  CCIfType<[i16], CCAssignToReg<[AX, DX, CX]>>,
  CCIfType<[i32], CCAssignToReg<[EAX, EDX, ECX]>>,

  // Otherwise, it is the same as the common X86 calling convention.
  CCDelegateTo<RetCC_X86Common>
]>;

// Intel_OCL_BI return-value convention.
def RetCC_Intel_OCL_BI : CallingConv<[
  // Vector types are returned in XMM0,XMM1,XMMM2 and XMM3.
  CCIfType<[f32, f64, v4i32, v2i64, v4f32, v2f64],
            CCAssignToReg<[XMM0,XMM1,XMM2,XMM3]>>,

  // 256-bit FP vectors
  // No more than 4 registers
  CCIfType<[v8f32, v4f64, v8i32, v4i64],
            CCAssignToReg<[YMM0,YMM1,YMM2,YMM3]>>,

  // 512-bit FP vectors
  CCIfType<[v16f32, v8f64, v16i32, v8i64],
            CCAssignToReg<[ZMM0,ZMM1,ZMM2,ZMM3]>>,

  // i32, i64 in the standard way
  CCDelegateTo<RetCC_X86Common>
]>;

// X86-32 HiPE return-value convention.
def RetCC_X86_32_HiPE : CallingConv<[
  // Promote all types to i32
  CCIfType<[i8, i16], CCPromoteToType<i32>>,

  // Return: HP, P, VAL1, VAL2
  CCIfType<[i32], CCAssignToReg<[ESI, EBP, EAX, EDX]>>
]>;

// X86-32 Vectorcall return-value convention.
def RetCC_X86_32_VectorCall : CallingConv<[
  // Floating Point types are returned in XMM0,XMM1,XMMM2 and XMM3.
  CCIfType<[f32, f64, f128],
            CCAssignToReg<[XMM0,XMM1,XMM2,XMM3]>>,

  // Return integers in the standard way.
  CCDelegateTo<RetCC_X86Common>
]>;

// X86-64 C return-value convention.
def RetCC_X86_64_C : CallingConv<[
  // The X86-64 calling convention always returns FP values in XMM0.
  CCIfType<[f32], CCAssignToReg<[XMM0, XMM1]>>,
  CCIfType<[f64], CCAssignToReg<[XMM0, XMM1]>>,
  CCIfType<[f128], CCAssignToReg<[XMM0, XMM1]>>,

  // MMX vector types are always returned in XMM0.
  CCIfType<[x86mmx], CCAssignToReg<[XMM0, XMM1]>>,

  CCIfSwiftError<CCIfType<[i64], CCAssignToReg<[R12]>>>,

  CCDelegateTo<RetCC_X86Common>
]>;

// X86-Win64 C return-value convention.
def RetCC_X86_Win64_C : CallingConv<[
  // The X86-Win64 calling convention always returns __m64 values in RAX.
  CCIfType<[x86mmx], CCBitConvertToType<i64>>,

  // Otherwise, everything is the same as 'normal' X86-64 C CC.
  CCDelegateTo<RetCC_X86_64_C>
]>;

// X86-64 vectorcall return-value convention.
def RetCC_X86_64_Vectorcall : CallingConv<[
  // Vectorcall calling convention always returns FP values in XMMs.
  CCIfType<[f32, f64, f128], 
    CCAssignToReg<[XMM0, XMM1, XMM2, XMM3]>>,

  // Otherwise, everything is the same as Windows X86-64 C CC.
  CCDelegateTo<RetCC_X86_Win64_C>
]>;

// X86-64 HiPE return-value convention.
def RetCC_X86_64_HiPE : CallingConv<[
  // Promote all types to i64
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,

  // Return: HP, P, VAL1, VAL2
  CCIfType<[i64], CCAssignToReg<[R15, RBP, RAX, RDX]>>
]>;

// X86-64 WebKit_JS return-value convention.
def RetCC_X86_64_WebKit_JS : CallingConv<[
  // Promote all types to i64
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,

  // Return: RAX
  CCIfType<[i64], CCAssignToReg<[RAX]>>
]>;

def RetCC_X86_64_Swift : CallingConv<[

  CCIfSwiftError<CCIfType<[i64], CCAssignToReg<[R12]>>>,

  // For integers, ECX, R8D can be used as extra return registers.
  CCIfType<[v1i1],  CCPromoteToType<i8>>,
  CCIfType<[i1],  CCPromoteToType<i8>>,
  CCIfType<[i8] , CCAssignToReg<[AL, DL, CL, R8B]>>,
  CCIfType<[i16], CCAssignToReg<[AX, DX, CX, R8W]>>,
  CCIfType<[i32], CCAssignToReg<[EAX, EDX, ECX, R8D]>>,
  CCIfType<[i64], CCAssignToReg<[RAX, RDX, RCX, R8]>>,

  // XMM0, XMM1, XMM2 and XMM3 can be used to return FP values.
  CCIfType<[f32], CCAssignToReg<[XMM0, XMM1, XMM2, XMM3]>>,
  CCIfType<[f64], CCAssignToReg<[XMM0, XMM1, XMM2, XMM3]>>,
  CCIfType<[f128], CCAssignToReg<[XMM0, XMM1, XMM2, XMM3]>>,

  // MMX vector types are returned in XMM0, XMM1, XMM2 and XMM3.
  CCIfType<[x86mmx], CCAssignToReg<[XMM0, XMM1, XMM2, XMM3]>>,
  CCDelegateTo<RetCC_X86Common>
]>;

// X86-64 AnyReg return-value convention. No explicit register is specified for
// the return-value. The register allocator is allowed and expected to choose
// any free register.
//
// This calling convention is currently only supported by the stackmap and
// patchpoint intrinsics. All other uses will result in an assert on Debug
// builds. On Release builds we fallback to the X86 C calling convention.
def RetCC_X86_64_AnyReg : CallingConv<[
  CCCustom<"CC_X86_AnyReg_Error">
]>;

// X86-64 HHVM return-value convention.
def RetCC_X86_64_HHVM: CallingConv<[
  // Promote all types to i64
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,

  // Return: could return in any GP register save RSP and R12.
  CCIfType<[i64], CCAssignToReg<[RBX, RBP, RDI, RSI, RDX, RCX, R8, R9,
                                 RAX, R10, R11, R13, R14, R15]>>
]>;


defm X86_32_RegCall :
	 X86_RegCall_base<RC_X86_32_RegCall>;
defm X86_Win64_RegCall :
     X86_RegCall_base<RC_X86_64_RegCall_Win>;
defm X86_SysV64_RegCall :
     X86_RegCall_base<RC_X86_64_RegCall_SysV>;

// This is the root return-value convention for the X86-32 backend.
def RetCC_X86_32 : CallingConv<[
  // If FastCC, use RetCC_X86_32_Fast.
  CCIfCC<"CallingConv::Fast", CCDelegateTo<RetCC_X86_32_Fast>>,
  // If HiPE, use RetCC_X86_32_HiPE.
  CCIfCC<"CallingConv::HiPE", CCDelegateTo<RetCC_X86_32_HiPE>>,
  CCIfCC<"CallingConv::X86_VectorCall", CCDelegateTo<RetCC_X86_32_VectorCall>>,
  CCIfCC<"CallingConv::X86_RegCall", CCDelegateTo<RetCC_X86_32_RegCall>>,

  // Otherwise, use RetCC_X86_32_C.
  CCDelegateTo<RetCC_X86_32_C>
]>;

// This is the root return-value convention for the X86-64 backend.
def RetCC_X86_64 : CallingConv<[
  // HiPE uses RetCC_X86_64_HiPE
  CCIfCC<"CallingConv::HiPE", CCDelegateTo<RetCC_X86_64_HiPE>>,

  // Handle JavaScript calls.
  CCIfCC<"CallingConv::WebKit_JS", CCDelegateTo<RetCC_X86_64_WebKit_JS>>,
  CCIfCC<"CallingConv::AnyReg", CCDelegateTo<RetCC_X86_64_AnyReg>>,

  // Handle Swift calls.
  CCIfCC<"CallingConv::Swift", CCDelegateTo<RetCC_X86_64_Swift>>,

  // Handle explicit CC selection
  CCIfCC<"CallingConv::Win64", CCDelegateTo<RetCC_X86_Win64_C>>,
  CCIfCC<"CallingConv::X86_64_SysV", CCDelegateTo<RetCC_X86_64_C>>,

  // Handle Vectorcall CC
  CCIfCC<"CallingConv::X86_VectorCall", CCDelegateTo<RetCC_X86_64_Vectorcall>>,

  // Handle HHVM calls.
  CCIfCC<"CallingConv::HHVM", CCDelegateTo<RetCC_X86_64_HHVM>>,

  CCIfCC<"CallingConv::X86_RegCall",
          CCIfSubtarget<"isTargetWin64()",
                        CCDelegateTo<RetCC_X86_Win64_RegCall>>>,
  CCIfCC<"CallingConv::X86_RegCall", CCDelegateTo<RetCC_X86_SysV64_RegCall>>,
          
  // Mingw64 and native Win64 use Win64 CC
  CCIfSubtarget<"isTargetWin64()", CCDelegateTo<RetCC_X86_Win64_C>>,

  // Otherwise, drop to normal X86-64 CC
  CCDelegateTo<RetCC_X86_64_C>
]>;

// This is the return-value convention used for the entire X86 backend.
def RetCC_X86 : CallingConv<[

  // Check if this is the Intel OpenCL built-ins calling convention
  CCIfCC<"CallingConv::Intel_OCL_BI", CCDelegateTo<RetCC_Intel_OCL_BI>>,

  CCIfSubtarget<"is64Bit()", CCDelegateTo<RetCC_X86_64>>,
  CCDelegateTo<RetCC_X86_32>
]>;

//===----------------------------------------------------------------------===//
// X86-64 Argument Calling Conventions
//===----------------------------------------------------------------------===//

def CC_X86_64_C : CallingConv<[
  // Handles byval parameters.
  CCIfByVal<CCPassByVal<8, 8>>,

  // Promote i1/i8/i16/v1i1 arguments to i32.
  CCIfType<[i1, i8, i16, v1i1], CCPromoteToType<i32>>,

  // The 'nest' parameter, if any, is passed in R10.
  CCIfNest<CCIfSubtarget<"isTarget64BitILP32()", CCAssignToReg<[R10D]>>>,
  CCIfNest<CCAssignToReg<[R10]>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i64], CCAssignToReg<[R13]>>>,

  // A SwiftError is passed in R12.
  CCIfSwiftError<CCIfType<[i64], CCAssignToReg<[R12]>>>,

  // For Swift Calling Convention, pass sret in %rax.
  CCIfCC<"CallingConv::Swift",
    CCIfSRet<CCIfType<[i64], CCAssignToReg<[RAX]>>>>,

  // The first 6 integer arguments are passed in integer registers.
  CCIfType<[i32], CCAssignToReg<[EDI, ESI, EDX, ECX, R8D, R9D]>>,
  CCIfType<[i64], CCAssignToReg<[RDI, RSI, RDX, RCX, R8 , R9 ]>>,

  // The first 8 MMX vector arguments are passed in XMM registers on Darwin.
  CCIfType<[x86mmx],
            CCIfSubtarget<"isTargetDarwin()",
            CCIfSubtarget<"hasSSE2()",
            CCPromoteToType<v2i64>>>>,

  // Boolean vectors of AVX-512 are passed in SIMD registers.
  // The call from AVX to AVX-512 function should work,
  // since the boolean types in AVX/AVX2 are promoted by default.
  CCIfType<[v2i1],  CCPromoteToType<v2i64>>,
  CCIfType<[v4i1],  CCPromoteToType<v4i32>>,
  CCIfType<[v8i1],  CCPromoteToType<v8i16>>,
  CCIfType<[v16i1], CCPromoteToType<v16i8>>,
  CCIfType<[v32i1], CCPromoteToType<v32i8>>,
  CCIfType<[v64i1], CCPromoteToType<v64i8>>,

  // The first 8 FP/Vector arguments are passed in XMM registers.
  CCIfType<[f32, f64, f128, v16i8, v8i16, v4i32, v2i64, v4f32, v2f64],
            CCIfSubtarget<"hasSSE1()",
            CCAssignToReg<[XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7]>>>,

  // The first 8 256-bit vector arguments are passed in YMM registers, unless
  // this is a vararg function.
  // FIXME: This isn't precisely correct; the x86-64 ABI document says that
  // fixed arguments to vararg functions are supposed to be passed in
  // registers.  Actually modeling that would be a lot of work, though.
  CCIfNotVarArg<CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64],
                          CCIfSubtarget<"hasAVX()",
                          CCAssignToReg<[YMM0, YMM1, YMM2, YMM3,
                                         YMM4, YMM5, YMM6, YMM7]>>>>,

  // The first 8 512-bit vector arguments are passed in ZMM registers.
  CCIfNotVarArg<CCIfType<[v64i8, v32i16, v16i32, v8i64, v16f32, v8f64],
            CCIfSubtarget<"hasAVX512()",
            CCAssignToReg<[ZMM0, ZMM1, ZMM2, ZMM3, ZMM4, ZMM5, ZMM6, ZMM7]>>>>,

  // Integer/FP values get stored in stack slots that are 8 bytes in size and
  // 8-byte aligned if there are no more registers to hold them.
  CCIfType<[i32, i64, f32, f64], CCAssignToStack<8, 8>>,

  // Long doubles get stack slots whose size and alignment depends on the
  // subtarget.
  CCIfType<[f80, f128], CCAssignToStack<0, 0>>,

  // Vectors get 16-byte stack slots that are 16-byte aligned.
  CCIfType<[v16i8, v8i16, v4i32, v2i64, v4f32, v2f64], CCAssignToStack<16, 16>>,

  // 256-bit vectors get 32-byte stack slots that are 32-byte aligned.
  CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64],
           CCAssignToStack<32, 32>>,

  // 512-bit vectors get 64-byte stack slots that are 64-byte aligned.
  CCIfType<[v16i32, v8i64, v16f32, v8f64],
           CCAssignToStack<64, 64>>
]>;

// Calling convention for X86-64 HHVM.
def CC_X86_64_HHVM : CallingConv<[
  // Use all/any GP registers for args, except RSP.
  CCIfType<[i64], CCAssignToReg<[RBX, R12, RBP, R15,
                                 RDI, RSI, RDX, RCX, R8, R9,
                                 RAX, R10, R11, R13, R14]>>
]>;

// Calling convention for helper functions in HHVM.
def CC_X86_64_HHVM_C : CallingConv<[
  // Pass the first argument in RBP.
  CCIfType<[i64], CCAssignToReg<[RBP]>>,

  // Otherwise it's the same as the regular C calling convention.
  CCDelegateTo<CC_X86_64_C>
]>;

// Calling convention used on Win64
def CC_X86_Win64_C : CallingConv<[
  // FIXME: Handle varargs.

  // Byval aggregates are passed by pointer
  CCIfByVal<CCPassIndirect<i64>>,

  // Promote i1/v1i1 arguments to i8.
  CCIfType<[i1, v1i1], CCPromoteToType<i8>>,

  // The 'nest' parameter, if any, is passed in R10.
  CCIfNest<CCAssignToReg<[R10]>>,

  // A SwiftError is passed in R12.
  CCIfSwiftError<CCIfType<[i64], CCAssignToReg<[R12]>>>,

  // 128 bit vectors are passed by pointer
  CCIfType<[v16i8, v8i16, v4i32, v2i64, v4f32, v2f64], CCPassIndirect<i64>>,


  // 256 bit vectors are passed by pointer
  CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64], CCPassIndirect<i64>>,

  // 512 bit vectors are passed by pointer
  CCIfType<[v16i32, v16f32, v8f64, v8i64], CCPassIndirect<i64>>,

  // Long doubles are passed by pointer
  CCIfType<[f80], CCPassIndirect<i64>>,

  // The first 4 MMX vector arguments are passed in GPRs.
  CCIfType<[x86mmx], CCBitConvertToType<i64>>,

  // The first 4 integer arguments are passed in integer registers.
  CCIfType<[i8 ], CCAssignToRegWithShadow<[CL  , DL  , R8B , R9B ],
                                          [XMM0, XMM1, XMM2, XMM3]>>,
  CCIfType<[i16], CCAssignToRegWithShadow<[CX  , DX  , R8W , R9W ],
                                          [XMM0, XMM1, XMM2, XMM3]>>,
  CCIfType<[i32], CCAssignToRegWithShadow<[ECX , EDX , R8D , R9D ],
                                          [XMM0, XMM1, XMM2, XMM3]>>,

  // Do not pass the sret argument in RCX, the Win64 thiscall calling
  // convention requires "this" to be passed in RCX.
  CCIfCC<"CallingConv::X86_ThisCall",
    CCIfSRet<CCIfType<[i64], CCAssignToRegWithShadow<[RDX , R8  , R9  ],
                                                     [XMM1, XMM2, XMM3]>>>>,

  CCIfType<[i64], CCAssignToRegWithShadow<[RCX , RDX , R8  , R9  ],
                                          [XMM0, XMM1, XMM2, XMM3]>>,

  // The first 4 FP/Vector arguments are passed in XMM registers.
  CCIfType<[f32, f64, v16i8, v8i16, v4i32, v2i64, v4f32, v2f64],
           CCAssignToRegWithShadow<[XMM0, XMM1, XMM2, XMM3],
                                   [RCX , RDX , R8  , R9  ]>>,

  // Integer/FP values get stored in stack slots that are 8 bytes in size and
  // 8-byte aligned if there are no more registers to hold them.
  CCIfType<[i8, i16, i32, i64, f32, f64], CCAssignToStack<8, 8>>
]>;

def CC_X86_Win64_VectorCall : CallingConv<[
  CCCustom<"CC_X86_64_VectorCall">,

  // Delegate to fastcall to handle integer types.
  CCDelegateTo<CC_X86_Win64_C>
]>;


def CC_X86_64_GHC : CallingConv<[
  // Promote i8/i16/i32 arguments to i64.
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,

  // Pass in STG registers: Base, Sp, Hp, R1, R2, R3, R4, R5, R6, SpLim
  CCIfType<[i64],
            CCAssignToReg<[R13, RBP, R12, RBX, R14, RSI, RDI, R8, R9, R15]>>,

  // Pass in STG registers: F1, F2, F3, F4, D1, D2
  CCIfType<[f32, f64, v16i8, v8i16, v4i32, v2i64, v4f32, v2f64],
            CCIfSubtarget<"hasSSE1()",
            CCAssignToReg<[XMM1, XMM2, XMM3, XMM4, XMM5, XMM6]>>>,
  // AVX
  CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64],
            CCIfSubtarget<"hasAVX()",
            CCAssignToReg<[YMM1, YMM2, YMM3, YMM4, YMM5, YMM6]>>>,
  // AVX-512
  CCIfType<[v64i8, v32i16, v16i32, v8i64, v16f32, v8f64],
            CCIfSubtarget<"hasAVX512()",
            CCAssignToReg<[ZMM1, ZMM2, ZMM3, ZMM4, ZMM5, ZMM6]>>>
]>;

def CC_X86_64_HiPE : CallingConv<[
  // Promote i8/i16/i32 arguments to i64.
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,

  // Pass in VM's registers: HP, P, ARG0, ARG1, ARG2, ARG3
  CCIfType<[i64], CCAssignToReg<[R15, RBP, RSI, RDX, RCX, R8]>>,

  // Integer/FP values get stored in stack slots that are 8 bytes in size and
  // 8-byte aligned if there are no more registers to hold them.
  CCIfType<[i32, i64, f32, f64], CCAssignToStack<8, 8>>
]>;

def CC_X86_64_WebKit_JS : CallingConv<[
  // Promote i8/i16 arguments to i32.
  CCIfType<[i8, i16], CCPromoteToType<i32>>,

  // Only the first integer argument is passed in register.
  CCIfType<[i32], CCAssignToReg<[EAX]>>,
  CCIfType<[i64], CCAssignToReg<[RAX]>>,

  // The remaining integer arguments are passed on the stack. 32bit integer and
  // floating-point arguments are aligned to 4 byte and stored in 4 byte slots.
  // 64bit integer and floating-point arguments are aligned to 8 byte and stored
  // in 8 byte stack slots.
  CCIfType<[i32, f32], CCAssignToStack<4, 4>>,
  CCIfType<[i64, f64], CCAssignToStack<8, 8>>
]>;

// No explicit register is specified for the AnyReg calling convention. The
// register allocator may assign the arguments to any free register.
//
// This calling convention is currently only supported by the stackmap and
// patchpoint intrinsics. All other uses will result in an assert on Debug
// builds. On Release builds we fallback to the X86 C calling convention.
def CC_X86_64_AnyReg : CallingConv<[
  CCCustom<"CC_X86_AnyReg_Error">
]>;

//===----------------------------------------------------------------------===//
// X86 C Calling Convention
//===----------------------------------------------------------------------===//

/// CC_X86_32_Vector_Common - In all X86-32 calling conventions, extra vector
/// values are spilled on the stack.
def CC_X86_32_Vector_Common : CallingConv<[
  // Other SSE vectors get 16-byte stack slots that are 16-byte aligned.
  CCIfType<[v16i8, v8i16, v4i32, v2i64, v4f32, v2f64], CCAssignToStack<16, 16>>,

  // 256-bit AVX vectors get 32-byte stack slots that are 32-byte aligned.
  CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64],
           CCAssignToStack<32, 32>>,

  // 512-bit AVX 512-bit vectors get 64-byte stack slots that are 64-byte aligned.
  CCIfType<[v64i8, v32i16, v16i32, v8i64, v16f32, v8f64],
           CCAssignToStack<64, 64>>
]>;

// CC_X86_32_Vector_Standard - The first 3 vector arguments are passed in
// vector registers
def CC_X86_32_Vector_Standard : CallingConv<[
  // SSE vector arguments are passed in XMM registers.
  CCIfNotVarArg<CCIfType<[v16i8, v8i16, v4i32, v2i64, v4f32, v2f64],
                CCAssignToReg<[XMM0, XMM1, XMM2]>>>,

  // AVX 256-bit vector arguments are passed in YMM registers.
  CCIfNotVarArg<CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64],
                CCIfSubtarget<"hasAVX()",
                CCAssignToReg<[YMM0, YMM1, YMM2]>>>>,

  // AVX 512-bit vector arguments are passed in ZMM registers.
  CCIfNotVarArg<CCIfType<[v64i8, v32i16, v16i32, v8i64, v16f32, v8f64],
                CCAssignToReg<[ZMM0, ZMM1, ZMM2]>>>,

  CCDelegateTo<CC_X86_32_Vector_Common>
]>;

// CC_X86_32_Vector_Darwin - The first 4 vector arguments are passed in
// vector registers.
def CC_X86_32_Vector_Darwin : CallingConv<[
  // SSE vector arguments are passed in XMM registers.
  CCIfNotVarArg<CCIfType<[v16i8, v8i16, v4i32, v2i64, v4f32, v2f64],
                CCAssignToReg<[XMM0, XMM1, XMM2, XMM3]>>>,

  // AVX 256-bit vector arguments are passed in YMM registers.
  CCIfNotVarArg<CCIfType<[v32i8, v16i16, v8i32, v4i64, v8f32, v4f64],
                CCIfSubtarget<"hasAVX()",
                CCAssignToReg<[YMM0, YMM1, YMM2, YMM3]>>>>,

  // AVX 512-bit vector arguments are passed in ZMM registers.
  CCIfNotVarArg<CCIfType<[v64i8, v32i16, v16i32, v8i64, v16f32, v8f64],
                CCAssignToReg<[ZMM0, ZMM1, ZMM2, ZMM3]>>>,

  CCDelegateTo<CC_X86_32_Vector_Common>
]>;

/// CC_X86_32_Common - In all X86-32 calling conventions, extra integers and FP
/// values are spilled on the stack.
def CC_X86_32_Common : CallingConv<[
  // Handles byval parameters.
  CCIfByVal<CCPassByVal<4, 4>>,

  // The first 3 float or double arguments, if marked 'inreg' and if the call
  // is not a vararg call and if SSE2 is available, are passed in SSE registers.
  CCIfNotVarArg<CCIfInReg<CCIfType<[f32,f64],
                CCIfSubtarget<"hasSSE2()",
                CCAssignToReg<[XMM0,XMM1,XMM2]>>>>>,

  // The first 3 __m64 vector arguments are passed in mmx registers if the
  // call is not a vararg call.
  CCIfNotVarArg<CCIfType<[x86mmx],
                CCAssignToReg<[MM0, MM1, MM2]>>>,

  // Integer/Float values get stored in stack slots that are 4 bytes in
  // size and 4-byte aligned.
  CCIfType<[i32, f32], CCAssignToStack<4, 4>>,

  // Doubles get 8-byte slots that are 4-byte aligned.
  CCIfType<[f64], CCAssignToStack<8, 4>>,

  // Long doubles get slots whose size depends on the subtarget.
  CCIfType<[f80], CCAssignToStack<0, 4>>,

  // Boolean vectors of AVX-512 are passed in SIMD registers.
  // The call from AVX to AVX-512 function should work,
  // since the boolean types in AVX/AVX2 are promoted by default.
  CCIfType<[v2i1],  CCPromoteToType<v2i64>>,
  CCIfType<[v4i1],  CCPromoteToType<v4i32>>,
  CCIfType<[v8i1],  CCPromoteToType<v8i16>>,
  CCIfType<[v16i1], CCPromoteToType<v16i8>>,
  CCIfType<[v32i1], CCPromoteToType<v32i8>>,
  CCIfType<[v64i1], CCPromoteToType<v64i8>>,

  // __m64 vectors get 8-byte stack slots that are 4-byte aligned. They are
  // passed in the parameter area.
  CCIfType<[x86mmx], CCAssignToStack<8, 4>>,

  // Darwin passes vectors in a form that differs from the i386 psABI
  CCIfSubtarget<"isTargetDarwin()", CCDelegateTo<CC_X86_32_Vector_Darwin>>,

  // Otherwise, drop to 'normal' X86-32 CC
  CCDelegateTo<CC_X86_32_Vector_Standard>
]>;

def CC_X86_32_C : CallingConv<[
  // Promote i1/i8/i16/v1i1 arguments to i32.
  CCIfType<[i1, i8, i16, v1i1], CCPromoteToType<i32>>,

  // The 'nest' parameter, if any, is passed in ECX.
  CCIfNest<CCAssignToReg<[ECX]>>,

  // The first 3 integer arguments, if marked 'inreg' and if the call is not
  // a vararg call, are passed in integer registers.
  CCIfNotVarArg<CCIfInReg<CCIfType<[i32], CCAssignToReg<[EAX, EDX, ECX]>>>>,

  // Otherwise, same as everything else.
  CCDelegateTo<CC_X86_32_Common>
]>;

def CC_X86_32_MCU : CallingConv<[
  // Handles byval parameters.  Note that, like FastCC, we can't rely on
  // the delegation to CC_X86_32_Common because that happens after code that
  // puts arguments in registers.
  CCIfByVal<CCPassByVal<4, 4>>,

  // Promote i1/i8/i16/v1i1 arguments to i32.
  CCIfType<[i1, i8, i16, v1i1], CCPromoteToType<i32>>,

  // If the call is not a vararg call, some arguments may be passed
  // in integer registers.
  CCIfNotVarArg<CCIfType<[i32], CCCustom<"CC_X86_32_MCUInReg">>>,

  // Otherwise, same as everything else.
  CCDelegateTo<CC_X86_32_Common>
]>;

def CC_X86_32_FastCall : CallingConv<[
  // Promote i1 to i8.
  CCIfType<[i1], CCPromoteToType<i8>>,

  // The 'nest' parameter, if any, is passed in EAX.
  CCIfNest<CCAssignToReg<[EAX]>>,

  // The first 2 integer arguments are passed in ECX/EDX
  CCIfInReg<CCIfType<[ i8], CCAssignToReg<[ CL,  DL]>>>,
  CCIfInReg<CCIfType<[i16], CCAssignToReg<[ CX,  DX]>>>,
  CCIfInReg<CCIfType<[i32], CCAssignToReg<[ECX, EDX]>>>,

  // Otherwise, same as everything else.
  CCDelegateTo<CC_X86_32_Common>
]>;

def CC_X86_Win32_VectorCall : CallingConv<[
  // Pass floating point in XMMs
  CCCustom<"CC_X86_32_VectorCall">,

  // Delegate to fastcall to handle integer types.
  CCDelegateTo<CC_X86_32_FastCall>
]>;

def CC_X86_32_ThisCall_Common : CallingConv<[
  // The first integer argument is passed in ECX
  CCIfType<[i32], CCAssignToReg<[ECX]>>,

  // Otherwise, same as everything else.
  CCDelegateTo<CC_X86_32_Common>
]>;

def CC_X86_32_ThisCall_Mingw : CallingConv<[
  // Promote i1/i8/i16/v1i1 arguments to i32.
  CCIfType<[i1, i8, i16, v1i1], CCPromoteToType<i32>>,

  CCDelegateTo<CC_X86_32_ThisCall_Common>
]>;

def CC_X86_32_ThisCall_Win : CallingConv<[
  // Promote i1/i8/i16/v1i1 arguments to i32.
  CCIfType<[i1, i8, i16, v1i1], CCPromoteToType<i32>>,

  // Pass sret arguments indirectly through stack.
  CCIfSRet<CCAssignToStack<4, 4>>,

  CCDelegateTo<CC_X86_32_ThisCall_Common>
]>;

def CC_X86_32_ThisCall : CallingConv<[
  CCIfSubtarget<"isTargetCygMing()", CCDelegateTo<CC_X86_32_ThisCall_Mingw>>,
  CCDelegateTo<CC_X86_32_ThisCall_Win>
]>;

def CC_X86_32_FastCC : CallingConv<[
  // Handles byval parameters.  Note that we can't rely on the delegation
  // to CC_X86_32_Common for this because that happens after code that
  // puts arguments in registers.
  CCIfByVal<CCPassByVal<4, 4>>,

  // Promote i1/i8/i16/v1i1 arguments to i32.
  CCIfType<[i1, i8, i16, v1i1], CCPromoteToType<i32>>,

  // The 'nest' parameter, if any, is passed in EAX.
  CCIfNest<CCAssignToReg<[EAX]>>,

  // The first 2 integer arguments are passed in ECX/EDX
  CCIfType<[i32], CCAssignToReg<[ECX, EDX]>>,

  // The first 3 float or double arguments, if the call is not a vararg
  // call and if SSE2 is available, are passed in SSE registers.
  CCIfNotVarArg<CCIfType<[f32,f64],
                CCIfSubtarget<"hasSSE2()",
                CCAssignToReg<[XMM0,XMM1,XMM2]>>>>,

  // Doubles get 8-byte slots that are 8-byte aligned.
  CCIfType<[f64], CCAssignToStack<8, 8>>,

  // Otherwise, same as everything else.
  CCDelegateTo<CC_X86_32_Common>
]>;

def CC_X86_32_GHC : CallingConv<[
  // Promote i8/i16 arguments to i32.
  CCIfType<[i8, i16], CCPromoteToType<i32>>,

  // Pass in STG registers: Base, Sp, Hp, R1
  CCIfType<[i32], CCAssignToReg<[EBX, EBP, EDI, ESI]>>
]>;

def CC_X86_32_HiPE : CallingConv<[
  // Promote i8/i16 arguments to i32.
  CCIfType<[i8, i16], CCPromoteToType<i32>>,

  // Pass in VM's registers: HP, P, ARG0, ARG1, ARG2
  CCIfType<[i32], CCAssignToReg<[ESI, EBP, EAX, EDX, ECX]>>,

  // Integer/Float values get stored in stack slots that are 4 bytes in
  // size and 4-byte aligned.
  CCIfType<[i32, f32], CCAssignToStack<4, 4>>
]>;

// X86-64 Intel OpenCL built-ins calling convention.
def CC_Intel_OCL_BI : CallingConv<[

  CCIfType<[i32], CCIfSubtarget<"isTargetWin64()", CCAssignToReg<[ECX, EDX, R8D, R9D]>>>,
  CCIfType<[i64], CCIfSubtarget<"isTargetWin64()", CCAssignToReg<[RCX, RDX, R8,  R9 ]>>>,

  CCIfType<[i32], CCIfSubtarget<"is64Bit()", CCAssignToReg<[EDI, ESI, EDX, ECX]>>>,
  CCIfType<[i64], CCIfSubtarget<"is64Bit()", CCAssignToReg<[RDI, RSI, RDX, RCX]>>>,

  CCIfType<[i32], CCAssignToStack<4, 4>>,

  // The SSE vector arguments are passed in XMM registers.
  CCIfType<[f32, f64, v4i32, v2i64, v4f32, v2f64],
           CCAssignToReg<[XMM0, XMM1, XMM2, XMM3]>>,

  // The 256-bit vector arguments are passed in YMM registers.
  CCIfType<[v8f32, v4f64, v8i32, v4i64],
           CCAssignToReg<[YMM0, YMM1, YMM2, YMM3]>>,

  // The 512-bit vector arguments are passed in ZMM registers.
  CCIfType<[v16f32, v8f64, v16i32, v8i64],
           CCAssignToReg<[ZMM0, ZMM1, ZMM2, ZMM3]>>,

  // Pass masks in mask registers
  CCIfType<[v16i1, v8i1], CCAssignToReg<[K1]>>,

  CCIfSubtarget<"isTargetWin64()", CCDelegateTo<CC_X86_Win64_C>>,
  CCIfSubtarget<"is64Bit()",       CCDelegateTo<CC_X86_64_C>>,
  CCDelegateTo<CC_X86_32_C>
]>;

def CC_X86_32_Intr : CallingConv<[
  CCAssignToStack<4, 4>
]>;

def CC_X86_64_Intr : CallingConv<[
  CCAssignToStack<8, 8>
]>;

//===----------------------------------------------------------------------===//
// X86 Root Argument Calling Conventions
//===----------------------------------------------------------------------===//

// This is the root argument convention for the X86-32 backend.
def CC_X86_32 : CallingConv<[
  // X86_INTR calling convention is valid in MCU target and should override the
  // MCU calling convention. Thus, this should be checked before isTargetMCU().
  CCIfCC<"CallingConv::X86_INTR", CCDelegateTo<CC_X86_32_Intr>>,
  CCIfSubtarget<"isTargetMCU()", CCDelegateTo<CC_X86_32_MCU>>,
  CCIfCC<"CallingConv::X86_FastCall", CCDelegateTo<CC_X86_32_FastCall>>,
  CCIfCC<"CallingConv::X86_VectorCall", CCDelegateTo<CC_X86_Win32_VectorCall>>,
  CCIfCC<"CallingConv::X86_ThisCall", CCDelegateTo<CC_X86_32_ThisCall>>,
  CCIfCC<"CallingConv::Fast", CCDelegateTo<CC_X86_32_FastCC>>,
  CCIfCC<"CallingConv::GHC", CCDelegateTo<CC_X86_32_GHC>>,
  CCIfCC<"CallingConv::HiPE", CCDelegateTo<CC_X86_32_HiPE>>,
  CCIfCC<"CallingConv::X86_RegCall", CCDelegateTo<CC_X86_32_RegCall>>,

  // Otherwise, drop to normal X86-32 CC
  CCDelegateTo<CC_X86_32_C>
]>;

// This is the root argument convention for the X86-64 backend.
def CC_X86_64 : CallingConv<[
  CCIfCC<"CallingConv::GHC", CCDelegateTo<CC_X86_64_GHC>>,
  CCIfCC<"CallingConv::HiPE", CCDelegateTo<CC_X86_64_HiPE>>,
  CCIfCC<"CallingConv::WebKit_JS", CCDelegateTo<CC_X86_64_WebKit_JS>>,
  CCIfCC<"CallingConv::AnyReg", CCDelegateTo<CC_X86_64_AnyReg>>,
  CCIfCC<"CallingConv::Win64", CCDelegateTo<CC_X86_Win64_C>>,
  CCIfCC<"CallingConv::X86_64_SysV", CCDelegateTo<CC_X86_64_C>>,
  CCIfCC<"CallingConv::X86_VectorCall", CCDelegateTo<CC_X86_Win64_VectorCall>>,
  CCIfCC<"CallingConv::HHVM", CCDelegateTo<CC_X86_64_HHVM>>,
  CCIfCC<"CallingConv::HHVM_C", CCDelegateTo<CC_X86_64_HHVM_C>>,
  CCIfCC<"CallingConv::X86_RegCall",
    CCIfSubtarget<"isTargetWin64()", CCDelegateTo<CC_X86_Win64_RegCall>>>,
  CCIfCC<"CallingConv::X86_RegCall", CCDelegateTo<CC_X86_SysV64_RegCall>>,
  CCIfCC<"CallingConv::X86_INTR", CCDelegateTo<CC_X86_64_Intr>>,

  // Mingw64 and native Win64 use Win64 CC
  CCIfSubtarget<"isTargetWin64()", CCDelegateTo<CC_X86_Win64_C>>,

  // Otherwise, drop to normal X86-64 CC
  CCDelegateTo<CC_X86_64_C>
]>;

// This is the argument convention used for the entire X86 backend.
def CC_X86 : CallingConv<[
  CCIfCC<"CallingConv::Intel_OCL_BI", CCDelegateTo<CC_Intel_OCL_BI>>,
  CCIfSubtarget<"is64Bit()", CCDelegateTo<CC_X86_64>>,
  CCDelegateTo<CC_X86_32>
]>;

//===----------------------------------------------------------------------===//
// Callee-saved Registers.
//===----------------------------------------------------------------------===//

def CSR_NoRegs : CalleeSavedRegs<(add)>;

def CSR_32 : CalleeSavedRegs<(add ESI, EDI, EBX, EBP)>;
def CSR_64 : CalleeSavedRegs<(add RBX, R12, R13, R14, R15, RBP)>;

def CSR_64_SwiftError : CalleeSavedRegs<(sub CSR_64, R12)>;

def CSR_32EHRet : CalleeSavedRegs<(add EAX, EDX, CSR_32)>;
def CSR_64EHRet : CalleeSavedRegs<(add RAX, RDX, CSR_64)>;

def CSR_Win64_NoSSE : CalleeSavedRegs<(add RBX, RBP, RDI, RSI, R12, R13, R14, R15)>;

def CSR_Win64 : CalleeSavedRegs<(add CSR_Win64_NoSSE,
                                     (sequence "XMM%u", 6, 15))>;

def CSR_Win64_SwiftError : CalleeSavedRegs<(sub CSR_Win64, R12)>;

// The function used by Darwin to obtain the address of a thread-local variable
// uses rdi to pass a single parameter and rax for the return value. All other
// GPRs are preserved.
def CSR_64_TLS_Darwin : CalleeSavedRegs<(add CSR_64, RCX, RDX, RSI,
                                             R8, R9, R10, R11)>;

// CSRs that are handled by prologue, epilogue.
def CSR_64_CXX_TLS_Darwin_PE : CalleeSavedRegs<(add RBP)>;

// CSRs that are handled explicitly via copies.
def CSR_64_CXX_TLS_Darwin_ViaCopy : CalleeSavedRegs<(sub CSR_64_TLS_Darwin, RBP)>;

// All GPRs - except r11
def CSR_64_RT_MostRegs : CalleeSavedRegs<(add CSR_64, RAX, RCX, RDX, RSI, RDI,
                                              R8, R9, R10, RSP)>;

// All registers - except r11
def CSR_64_RT_AllRegs     : CalleeSavedRegs<(add CSR_64_RT_MostRegs,
                                                 (sequence "XMM%u", 0, 15))>;
def CSR_64_RT_AllRegs_AVX : CalleeSavedRegs<(add CSR_64_RT_MostRegs,
                                                 (sequence "YMM%u", 0, 15))>;

def CSR_64_MostRegs : CalleeSavedRegs<(add RBX, RCX, RDX, RSI, RDI, R8, R9, R10,
                                           R11, R12, R13, R14, R15, RBP,
                                           (sequence "XMM%u", 0, 15))>;

def CSR_32_AllRegs     : CalleeSavedRegs<(add EAX, EBX, ECX, EDX, EBP, ESI,
                                              EDI)>;
def CSR_32_AllRegs_SSE : CalleeSavedRegs<(add CSR_32_AllRegs,
                                              (sequence "XMM%u", 0, 7))>;
def CSR_32_AllRegs_AVX : CalleeSavedRegs<(add CSR_32_AllRegs,
                                              (sequence "YMM%u", 0, 7))>;
def CSR_32_AllRegs_AVX512 : CalleeSavedRegs<(add CSR_32_AllRegs,
                                                 (sequence "ZMM%u", 0, 7),
                                                 (sequence "K%u", 0, 7))>;

def CSR_64_AllRegs     : CalleeSavedRegs<(add CSR_64_MostRegs, RAX)>;
def CSR_64_AllRegs_NoSSE : CalleeSavedRegs<(add RAX, RBX, RCX, RDX, RSI, RDI, R8, R9,
                                                R10, R11, R12, R13, R14, R15, RBP)>;
def CSR_64_AllRegs_AVX : CalleeSavedRegs<(sub (add CSR_64_MostRegs, RAX,
                                                   (sequence "YMM%u", 0, 15)),
                                              (sequence "XMM%u", 0, 15))>;
def CSR_64_AllRegs_AVX512 : CalleeSavedRegs<(sub (add CSR_64_MostRegs, RAX,
                                                      (sequence "ZMM%u", 0, 31),
                                                      (sequence "K%u", 0, 7)),
                                                 (sequence "XMM%u", 0, 15))>;

// Standard C + YMM6-15
def CSR_Win64_Intel_OCL_BI_AVX : CalleeSavedRegs<(add RBX, RBP, RDI, RSI, R12,
                                                  R13, R14, R15,
                                                  (sequence "YMM%u", 6, 15))>;

def CSR_Win64_Intel_OCL_BI_AVX512 : CalleeSavedRegs<(add RBX, RBP, RDI, RSI,
                                                     R12, R13, R14, R15,
                                                     (sequence "ZMM%u", 6, 21),
                                                     K4, K5, K6, K7)>;
//Standard C + XMM 8-15
def CSR_64_Intel_OCL_BI       : CalleeSavedRegs<(add CSR_64,
                                                 (sequence "XMM%u", 8, 15))>;

//Standard C + YMM 8-15
def CSR_64_Intel_OCL_BI_AVX    : CalleeSavedRegs<(add CSR_64,
                                                  (sequence "YMM%u", 8, 15))>;

def CSR_64_Intel_OCL_BI_AVX512 : CalleeSavedRegs<(add RBX, RDI, RSI, R14, R15,
                                                  (sequence "ZMM%u", 16, 31),
                                                  K4, K5, K6, K7)>;

// Only R12 is preserved for PHP calls in HHVM.
def CSR_64_HHVM : CalleeSavedRegs<(add R12)>;

// Register calling convention preserves few GPR and XMM8-15
def CSR_32_RegCall_NoSSE : CalleeSavedRegs<(add ESI, EDI, EBX, EBP, ESP)>;
def CSR_32_RegCall       : CalleeSavedRegs<(add CSR_32_RegCall_NoSSE,
                                           (sequence "XMM%u", 4, 7))>;                                            
def CSR_Win64_RegCall_NoSSE : CalleeSavedRegs<(add RBX, RBP, RSP,
                                              (sequence "R%u", 10, 15))>;
def CSR_Win64_RegCall       : CalleeSavedRegs<(add CSR_Win64_RegCall_NoSSE,                                  
                                              (sequence "XMM%u", 8, 15))>;
def CSR_SysV64_RegCall_NoSSE : CalleeSavedRegs<(add RBX, RBP, RSP,
                                               (sequence "R%u", 12, 15))>;
def CSR_SysV64_RegCall       : CalleeSavedRegs<(add CSR_SysV64_RegCall_NoSSE,               
                                               (sequence "XMM%u", 8, 15))>;
                                               
