//===-- extendsfdf2vfp.S - Implement extendsfdf2vfp -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern double __extendsfdf2vfp(float a);
//
// Converts single precision float to double precision result.
// Uses Darwin calling convention where a single precision parameter is 
// passed in a GPR and a double precision result is returned in R0/R1 pair.
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__extendsfdf2vfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vcvt.f64.f32 d0, s0
#else
	vmov	s15, r0      // load float register from R0
	vcvt.f64.f32 d7, s15 // convert single to double
	vmov	r0, r1, d7   // return result in r0/r1 pair
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__extendsfdf2vfp)

NO_EXEC_STACK_DIRECTIVE

