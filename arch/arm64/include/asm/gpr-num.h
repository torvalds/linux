/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_GPR_NUM_H
#define __ASM_GPR_NUM_H

#ifdef __ASSEMBLER__

	.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	.equ	.L__gpr_num_x\num, \num
	.equ	.L__gpr_num_w\num, \num
	.endr
	.equ	.L__gpr_num_xzr, 31
	.equ	.L__gpr_num_wzr, 31

#define __GPR_NUM(gpr)		(.L__gpr_num_##gpr)

#else /* __ASSEMBLER__ */

#define __DEFINE_ASM_GPR_NUMS					\
"	.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n" \
"	.equ	.L__gpr_num_x\\num, \\num\n"			\
"	.equ	.L__gpr_num_w\\num, \\num\n"			\
"	.endr\n"						\
"	.equ	.L__gpr_num_xzr, 31\n"				\
"	.equ	.L__gpr_num_wzr, 31\n"

#define __DEFINE_ASM_GPR_ALIASES									\
"	.irp	n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n"	\
"	wx\\n	.req	w\\n\n"										\
"	.endr\n"

#define __GPR_NUM(gpr)		"(.L__gpr_num_" gpr ")"

#endif /* __ASSEMBLER__ */

#endif /* __ASM_GPR_NUM_H */
