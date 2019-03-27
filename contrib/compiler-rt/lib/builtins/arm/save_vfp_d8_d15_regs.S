//===-- save_restore_regs.S - Implement save/restore* ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// When compiling C++ functions that need to handle thrown exceptions the
// compiler is required to save all registers and call __Unwind_SjLj_Register
// in the function prolog.  But when compiling for thumb1, there are
// no instructions to access the floating point registers, so the
// compiler needs to add a call to the helper function _save_vfp_d8_d15_regs
// written in ARM to save the float registers.  In the epilog, the compiler
// must also add a call to __restore_vfp_d8_d15_regs to restore those registers.
//

	.text
	.syntax unified

//
// Save registers d8-d15 onto stack
//
	.p2align 2
DEFINE_COMPILERRT_PRIVATE_FUNCTION(__save_vfp_d8_d15_regs)
	vstmdb	sp!, {d8-d15}           // push registers d8-d15 onto stack
	bx      lr                      // return to prolog
END_COMPILERRT_FUNCTION(__save_vfp_d8_d15_regs)

NO_EXEC_STACK_DIRECTIVE

