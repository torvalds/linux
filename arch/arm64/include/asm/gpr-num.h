/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_GPR_NUM_H
#define __ASM_GPR_NUM_H

#ifdef __ASSEMBLY__

	.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__gpr_num_x\num, \num
	.equ	.L__gpr_num_w\num, \num
	.endr
	.equ	.L__gpr_num_xzr, 31
	.equ	.L__gpr_num_wzr, 31

#else /* __ASSEMBLY__ */

#define __DEFINE_ASM_GPR_NUMS					\
"	.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n" \
"	.equ	.L__gpr_num_x\\num, \\num\n"			\
"	.equ	.L__gpr_num_w\\num, \\num\n"			\
"	.endr\n"						\
"	.equ	.L__gpr_num_xzr, 31\n"				\
"	.equ	.L__gpr_num_wzr, 31\n"

#endif /* __ASSEMBLY__ */

#endif /* __ASM_GPR_NUM_H */
