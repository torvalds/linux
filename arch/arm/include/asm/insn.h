/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARM_INSN_H
#define __ASM_ARM_INSN_H

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
