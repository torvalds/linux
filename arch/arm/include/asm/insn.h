/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARM_INSN_H
#define __ASM_ARM_INSN_H

#include <linux/types.h>

/*
 * Avoid a literal load by emitting a sequence of ADD/LDR instructions with the
 * appropriate relocations. The combined sequence has a range of -/+ 256 MiB,
 * which should be sufficient for the core kernel as well as modules loaded
 * into the module region. (Not supported by LLD before release 14)
 */
#define LOAD_SYM_ARMV6(reg, sym)					\
	"	.globl	" #sym "				\n\t"	\
	"	.reloc	10f, R_ARM_ALU_PC_G0_NC, " #sym "	\n\t"	\
	"	.reloc	11f, R_ARM_ALU_PC_G1_NC, " #sym "	\n\t"	\
	"	.reloc	12f, R_ARM_LDR_PC_G2, " #sym "		\n\t"	\
	"10:	sub	" #reg ", pc, #8			\n\t"	\
	"11:	sub	" #reg ", " #reg ", #4			\n\t"	\
	"12:	ldr	" #reg ", [" #reg ", #0]		\n\t"

static inline unsigned long
arm_gen_nop(void)
{
#ifdef CONFIG_THUMB2_KERNEL
	return 0xf3af8000; /* nop.w */
#else
	return 0xe1a00000; /* mov r0, r0 */
#endif
}

unsigned long
__arm_gen_branch(unsigned long pc, unsigned long addr, bool link, bool warn);

static inline unsigned long
arm_gen_branch(unsigned long pc, unsigned long addr)
{
	return __arm_gen_branch(pc, addr, false, true);
}

static inline unsigned long
arm_gen_branch_link(unsigned long pc, unsigned long addr, bool warn)
{
	return __arm_gen_branch(pc, addr, true, warn);
}

#endif
