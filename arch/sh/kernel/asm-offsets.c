// SPDX-License-Identifier: GPL-2.0
/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/kbuild.h>
#include <linux/suspend.h>

#include <asm/thread_info.h>
#include <asm/suspend.h>

int main(void)
{
	/* offsets into the thread_info struct */
	DEFINE(TI_TASK,		offsetof(struct thread_info, task));
	DEFINE(TI_FLAGS,	offsetof(struct thread_info, flags));
	DEFINE(TI_CPU,		offsetof(struct thread_info, cpu));
	DEFINE(TI_PRE_COUNT,	offsetof(struct thread_info, preempt_count));
	DEFINE(TI_SIZE,		sizeof(struct thread_info));

#ifdef CONFIG_HIBERNATION
	DEFINE(PBE_ADDRESS, offsetof(struct pbe, address));
	DEFINE(PBE_ORIG_ADDRESS, offsetof(struct pbe, orig_address));
	DEFINE(PBE_NEXT, offsetof(struct pbe, next));
	DEFINE(SWSUSP_ARCH_REGS_SIZE, sizeof(struct swsusp_arch_regs));
#endif

	DEFINE(SH_SLEEP_MODE, offsetof(struct sh_sleep_data, mode));
	DEFINE(SH_SLEEP_SF_PRE, offsetof(struct sh_sleep_data, sf_pre));
	DEFINE(SH_SLEEP_SF_POST, offsetof(struct sh_sleep_data, sf_post));
	DEFINE(SH_SLEEP_RESUME, offsetof(struct sh_sleep_data, resume));
	DEFINE(SH_SLEEP_VBR, offsetof(struct sh_sleep_data, vbr));
	DEFINE(SH_SLEEP_SPC, offsetof(struct sh_sleep_data, spc));
	DEFINE(SH_SLEEP_SR, offsetof(struct sh_sleep_data, sr));
	DEFINE(SH_SLEEP_SP, offsetof(struct sh_sleep_data, sp));
	DEFINE(SH_SLEEP_BASE_ADDR, offsetof(struct sh_sleep_data, addr));
	DEFINE(SH_SLEEP_BASE_DATA, offsetof(struct sh_sleep_data, data));
	DEFINE(SH_SLEEP_REG_STBCR, offsetof(struct sh_sleep_regs, stbcr));
	DEFINE(SH_SLEEP_REG_BAR, offsetof(struct sh_sleep_regs, bar));
	DEFINE(SH_SLEEP_REG_PTEH, offsetof(struct sh_sleep_regs, pteh));
	DEFINE(SH_SLEEP_REG_PTEL, offsetof(struct sh_sleep_regs, ptel));
	DEFINE(SH_SLEEP_REG_TTB, offsetof(struct sh_sleep_regs, ttb));
	DEFINE(SH_SLEEP_REG_TEA, offsetof(struct sh_sleep_regs, tea));
	DEFINE(SH_SLEEP_REG_MMUCR, offsetof(struct sh_sleep_regs, mmucr));
	DEFINE(SH_SLEEP_REG_PTEA, offsetof(struct sh_sleep_regs, ptea));
	DEFINE(SH_SLEEP_REG_PASCR, offsetof(struct sh_sleep_regs, pascr));
	DEFINE(SH_SLEEP_REG_IRMCR, offsetof(struct sh_sleep_regs, irmcr));
	DEFINE(SH_SLEEP_REG_CCR, offsetof(struct sh_sleep_regs, ccr));
	DEFINE(SH_SLEEP_REG_RAMCR, offsetof(struct sh_sleep_regs, ramcr));
	return 0;
}
