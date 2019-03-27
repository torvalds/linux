//===-- fixunssfsivfp.S - Implement fixunssfsivfp -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern unsigned int __fixunssfsivfp(float a);
//
// Converts single precision float to a 32-bit unsigned int rounding towards 
// zero. All negative values become zero.
// Uses Darwin calling convention where a single precision parameter is 
// passed in a GPR..
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__fixunssfsivfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vcvt.u32.f32 s0, s0
	vmov r0, s0
#else
	vmov	s15, r0        // load float register from R0
	vcvt.u32.f32 s15, s15  // convert single to 32-bit unsigned into s15
	vmov	r0, s15	       // move s15 to result register
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__fixunssfsivfp)

NO_EXEC_STACK_DIRECTIVE

