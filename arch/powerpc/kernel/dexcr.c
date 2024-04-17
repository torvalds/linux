// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/prctl.h>
#include <linux/sched.h>

#include <asm/cpu_has_feature.h>
#include <asm/cputable.h>
#include <asm/processor.h>
#include <asm/reg.h>

static int __init init_task_dexcr(void)
{
	if (!early_cpu_has_feature(CPU_FTR_ARCH_31))
		return 0;

	current->thread.dexcr_onexec = mfspr(SPRN_DEXCR);

	return 0;
}
early_initcall(init_task_dexcr)
