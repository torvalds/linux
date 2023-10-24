// SPDX-License-Identifier: GPL-2.0-or-later

#include <asm/kup.h>
#include <asm/smp.h>

void setup_kuap(bool disabled)
{
	if (!disabled) {
		update_user_segments(mfsr(0) | SR_KS);
		isync();        /* Context sync required after mtsr() */
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
