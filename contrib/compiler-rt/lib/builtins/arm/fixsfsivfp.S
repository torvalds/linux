//===-- fixsfsivfp.S - Implement fixsfsivfp -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern int __fixsfsivfp(float a);
//
// Converts single precision float to a 32-bit int rounding towards zero.
// Uses Darwin calling convention where a single precision parameter is 
// passed in a GPR..
//
	.syntax unified
	.p2align 2
DEFINE_COMPILERRT_FUNCTION(__fixsfsivfp)
#if defined(COMPILER_RT_ARMHF_TARGET)
	vcvt.s32.f32 s0, s0
	vmov r0, s0
#else
	vmov	s15, r0        // load float register from R0
	vcvt.s32.f32 s15, s15  // convert single to 32-bit int into s15
	vmov	r0, s15	       // move s15 to result register
#endif
	bx	lr
END_COMPILERRT_FUNCTION(__fixsfsivfp)

NO_EXEC_STACK_DIRECTIVE

