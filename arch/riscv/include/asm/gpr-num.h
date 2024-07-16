/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_GPR_NUM_H
#define __ASM_GPR_NUM_H

#ifdef __ASSEMBLY__

	.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	.equ	.L__gpr_num_x\num, \num
	.endr

	.equ	.L__gpr_num_zero,	0
	.equ	.L__gpr_num_ra,		1
	.equ	.L__gpr_num_sp,		2
	.equ	.L__gpr_num_gp,		3
	.equ	.L__gpr_num_tp,		4
	.equ	.L__gpr_num_t0,		5
	.equ	.L__gpr_num_t1,		6
	.equ	.L__gpr_num_t2,		7
	.equ	.L__gpr_num_s0,		8
	.equ	.L__gpr_num_s1,		9
	.equ	.L__gpr_num_a0,		10
	.equ	.L__gpr_num_a1,		11
	.equ	.L__gpr_num_a2,		12
	.equ	.L__gpr_num_a3,		13
	.equ	.L__gpr_num_a4,		14
	.equ	.L__gpr_num_a5,		15
	.equ	.L__gpr_num_a6,		16
	.equ	.L__gpr_num_a7,		17
	.equ	.L__gpr_num_s2,		18
	.equ	.L__gpr_num_s3,		19
	.equ	.L__gpr_num_s4,		20
	.equ	.L__gpr_num_s5,		21
	.equ	.L__gpr_num_s6,		22
	.equ	.L__gpr_num_s7,		23
	.equ	.L__gpr_num_s8,		24
	.equ	.L__gpr_num_s9,		25
	.equ	.L__gpr_num_s10,	26
	.equ	.L__gpr_num_s11,	27
	.equ	.L__gpr_num_t3,		28
	.equ	.L__gpr_num_t4,		29
	.equ	.L__gpr_num_t5,		30
	.equ	.L__gpr_num_t6,		31

#else /* __ASSEMBLY__ */

#define __DEFINE_ASM_GPR_NUMS					\
"	.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n" \
"	.equ	.L__gpr_num_x\\num, \\num\n"			\
"	.endr\n"						\
"	.equ	.L__gpr_num_zero,	0\n"			\
"	.equ	.L__gpr_num_ra,		1\n"			\
"	.equ	.L__gpr_num_sp,		2\n"			\
"	.equ	.L__gpr_num_gp,		3\n"			\
"	.equ	.L__gpr_num_tp,		4\n"			\
"	.equ	.L__gpr_num_t0,		5\n"			\
"	.equ	.L__gpr_num_t1,		6\n"			\
"	.equ	.L__gpr_num_t2,		7\n"			\
"	.equ	.L__gpr_num_s0,		8\n"			\
"	.equ	.L__gpr_num_s1,		9\n"			\
"	.equ	.L__gpr_num_a0,		10\n"			\
"	.equ	.L__gpr_num_a1,		11\n"			\
"	.equ	.L__gpr_num_a2,		12\n"			\
"	.equ	.L__gpr_num_a3,		13\n"			\
"	.equ	.L__gpr_num_a4,		14\n"			\
"	.equ	.L__gpr_num_a5,		15\n"			\
"	.equ	.L__gpr_num_a6,		16\n"			\
"	.equ	.L__gpr_num_a7,		17\n"			\
"	.equ	.L__gpr_num_s2,		18\n"			\
"	.equ	.L__gpr_num_s3,		19\n"			\
"	.equ	.L__gpr_num_s4,		20\n"			\
"	.equ	.L__gpr_num_s5,		21\n"			\
"	.equ	.L__gpr_num_s6,		22\n"			\
"	.equ	.L__gpr_num_s7,		23\n"			\
"	.equ	.L__gpr_num_s8,		24\n"			\
"	.equ	.L__gpr_num_s9,		25\n"			\
"	.equ	.L__gpr_num_s10,	26\n"			\
"	.equ	.L__gpr_num_s11,	27\n"			\
"	.equ	.L__gpr_num_t3,		28\n"			\
"	.equ	.L__gpr_num_t4,		29\n"			\
"	.equ	.L__gpr_num_t5,		30\n"			\
"	.equ	.L__gpr_num_t6,		31\n"

#endif /* __ASSEMBLY__ */

#endif /* __ASM_GPR_NUM_H */
