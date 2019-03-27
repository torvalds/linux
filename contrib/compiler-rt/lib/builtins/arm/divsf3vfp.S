//===-- divsf3vfp.S - Implement divsf3vfp ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern float __divsf3vfp(float a, float b);
//
// Divides two single precision floating point numbers using the Darwin
// calling convention where single arguments are passsed like 32-bit ints.
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__divsf3vfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vdiv.f32 s0, s0, s1
#else
	vmov	s14, r0		// move first param from r0 into float register
	vmov	s15, r1		// move second param from r1 into float register
	vdiv.f32 s13, s14, s15
	vmov	r0, s13		// move result back to r0
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__divsf3vfp)

NO_EXEC_STACK_DIRECTIVE

