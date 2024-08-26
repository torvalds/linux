// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains the routines for initializing kernel userspace protection
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/smp.h>

#include <asm/kup.h>
#include <asm/smp.h>

#ifdef CONFIG_PPC_KUAP
void setup_kuap(bool disabled)
{
	if (disabled) {
		if (smp_processor_id() == boot_cpuid)
			cur_cpu_spec->mmu_features &= ~MMU_FTR_KUAP;
		return;
	}

	pr_info("Activating Kernel Userspace Access Protection\n");

	prevent_user_access(KUAP_READ_WRITE);
}
#endif
