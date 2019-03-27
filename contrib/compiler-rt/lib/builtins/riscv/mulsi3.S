//===--- mulsi3.S - Integer multiplication routines routines ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#if !defined(__riscv_mul) && __riscv_xlen == 32
	.text
	.align 2

	.globl __mulsi3
	.type  __mulsi3, @function
__mulsi3:
	mv     a2, a0
	mv     a0, zero
.L1:
	andi   a3, a1, 1
	beqz   a3, .L2
	add    a0, a0, a2
.L2:
	srli   a1, a1, 1
	slli   a2, a2, 1
	bnez   a1, .L1
	ret
#endif
