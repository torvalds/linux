/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_GPR_NUM_H
#define __ASM_GPR_NUM_H

#ifdef __ASSEMBLY__

	.equ	.L__gpr_num_zero, 0
	.irp	num,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	.equ	.L__gpr_num_$r\num, \num
	.endr

	/* ABI names of registers */
	.equ	.L__gpr_num_$ra, 1
	.equ	.L__gpr_num_$tp, 2
	.equ	.L__gpr_num_$sp, 3
	.irp	num,0,1,2,3,4,5,6,7
	.equ	.L__gpr_num_$a\num, 4 + \num
	.endr
	.irp	num,0,1,2,3,4,5,6,7,8
	.equ	.L__gpr_num_$t\num, 12 + \num
	.endr
	.equ	.L__gpr_num_$s9, 22
	.equ	.L__gpr_num_$fp, 22
	.irp	num,0,1,2,3,4,5,6,7,8
	.equ	.L__gpr_num_$s\num, 23 + \num
	.endr

#else /* __ASSEMBLY__ */

#define __DEFINE_ASM_GPR_NUMS					\
"	.equ	.L__gpr_num_zero, 0\n"				\
"	.irp	num,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n" \
"	.equ	.L__gpr_num_$r\\num, \\num\n"			\
"	.endr\n"						\
"	.equ	.L__gpr_num_$ra, 1\n"				\
"	.equ	.L__gpr_num_$tp, 2\n"				\
"	.equ	.L__gpr_num_$sp, 3\n"				\
"	.irp	num,0,1,2,3,4,5,6,7\n"				\
"	.equ	.L__gpr_num_$a\\num, 4 + \\num\n"		\
"	.endr\n"						\
"	.irp	num,0,1,2,3,4,5,6,7,8\n"			\
"	.equ	.L__gpr_num_$t\\num, 12 + \\num\n"		\
"	.endr\n"						\
"	.equ	.L__gpr_num_$s9, 22\n"				\
"	.equ	.L__gpr_num_$fp, 22\n"				\
"	.irp	num,0,1,2,3,4,5,6,7,8\n"			\
"	.equ	.L__gpr_num_$s\\num, 23 + \\num\n"		\
"	.endr\n"						\

#endif /* __ASSEMBLY__ */

#endif /* __ASM_GPR_NUM_H */
