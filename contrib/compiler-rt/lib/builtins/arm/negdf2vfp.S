//===-- negdf2vfp.S - Implement negdf2vfp ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern double __negdf2vfp(double a, double b);
//
// Returns the negation a double precision floating point numbers using the 
// Darwin calling convention where double arguments are passsed in GPR pairs.
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__negdf2vfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vneg.f64 d0, d0
#else
	eor	r1, r1, #-2147483648	// flip sign bit on double in r0/r1 pair
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__negdf2vfp)

NO_EXEC_STACK_DIRECTIVE

