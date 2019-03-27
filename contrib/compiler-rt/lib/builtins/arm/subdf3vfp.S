//===-- subdf3vfp.S - Implement subdf3vfp ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern double __subdf3vfp(double a, double b);
//
// Returns difference between two double precision floating point numbers using 
// the Darwin calling convention where double arguments are passsed in GPR pairs
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__subdf3vfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vsub.f64 d0, d0, d1
#else
	vmov 	d6, r0, r1         // move first param from r0/r1 pair into d6
	vmov 	d7, r2, r3         // move second param from r2/r3 pair into d7
	vsub.f64 d6, d6, d7		
	vmov 	r0, r1, d6         // move result back to r0/r1 pair
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__subdf3vfp)

NO_EXEC_STACK_DIRECTIVE

