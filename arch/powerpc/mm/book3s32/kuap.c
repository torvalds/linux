// SPDX-License-Identifier: GPL-2.0-or-later

#include <asm/kup.h>
#include <asm/smp.h>

void kuap_lock_all_ool(void)
{
	kuap_lock_all();
}
EXPORT_SYMBOL(kuap_lock_all_ool);

void kuap_unlock_all_ool(void)
{
	kuap_unlock_all();
}
EXPORT_SYMBOL(kuap_unlock_all_ool);

void setup_kuap(bool disabled)
{
	if (!disabled) {
		kuap_lock_all_ool();
		init_mm.context.sr0 |= SR_KS;
		current->thread.sr0 |= SR_KS;
	}

	if (smp_processor_id() != boot_cpuid)
		return;

	if (disabled)
		cur_cpu_spec->mmu_features &= ~MMU_FTR_KUAP;
	else
		pr_info("Activating Kernel Userspace Access Protection\n");
}
