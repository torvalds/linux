//===-- negsf2vfp.S - Implement negsf2vfp ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern float __negsf2vfp(float a);
//
// Returns the negation of a single precision floating point numbers using the 
// Darwin calling convention where single arguments are passsed like 32-bit ints
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__negsf2vfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vneg.f32 s0, s0
#else
	eor	r0, r0, #-2147483648	// flip sign bit on float in r0
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__negsf2vfp)

NO_EXEC_STACK_DIRECTIVE

