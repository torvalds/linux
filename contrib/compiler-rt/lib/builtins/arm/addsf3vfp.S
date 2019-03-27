//===-- addsf3vfp.S - Implement addsf3vfp ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern float __addsf3vfp(float a, float b);
//
// Adds two single precision floating point numbers using the Darwin
// calling convention where single arguments are passsed in GPRs
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__addsf3vfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vadd.f32 s0, s0, s1
#else
	vmov	s14, r0		// move first param from r0 into float register
	vmov	s15, r1		// move second param from r1 into float register
	vadd.f32 s14, s14, s15
	vmov	r0, s14		// move result back to r0
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__addsf3vfp)

NO_EXEC_STACK_DIRECTIVE

