/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_STATIC_CALL_H
#define _ASM_POWERPC_STATIC_CALL_H

#define __PPC_SCT(name, inst)					\
	asm(".pushsection .text, \"ax\"				\n"	\
	    ".align 5						\n"	\
	    ".globl " STATIC_CALL_TRAMP_STR(name) "		\n"	\
	    STATIC_CALL_TRAMP_STR(name) ":			\n"	\
	    inst "						\n"	\
	    "	lis	12,2f@ha				\n"	\
	    "	lwz	12,2f@l(12)				\n"	\
	    "	mtctr	12					\n"	\
	    "	bctr						\n"	\
	    "1:	li	3, 0					\n"	\
	    "	blr						\n"	\
	    "2:	.long 0						\n"	\
	    ".type " STATIC_CALL_TRAMP_STR(name) ", @function	\n"	\
	    ".size " STATIC_CALL_TRAMP_STR(name) ", . - " STATIC_CALL_TRAMP_STR(name) " \n" \
	    ".popsection					\n")

#define PPC_SCT_RET0		20		/* Offset of label 1 */
#define PPC_SCT_DATA		28		/* Offset of label 2 */

#define ARCH_DEFINE_STATIC_CALL_TRAMP(name, func)	__PPC_SCT(name, "b " #func)
#define ARCH_DEFINE_STATIC_CALL_NULL_TRAMP(name)	__PPC_SCT(name, "blr")

#endif /* _ASM_POWERPC_STATIC_CALL_H */
