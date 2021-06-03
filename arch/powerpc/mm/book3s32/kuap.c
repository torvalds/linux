// SPDX-License-Identifier: GPL-2.0-or-later

#include <asm/kup.h>
#include <asm/smp.h>

void __init setup_kuap(bool disabled)
{
	kuap_update_sr(mfsr(0) | SR_KS, 0, TASK_SIZE);

	if (smp_processor_id() != boot_cpuid)
		return;

	pr_info("Activating Kernel Userspace Access Protection\n");

	if (disabled)
		pr_warn("KUAP cannot be disabled yet on 6xx when compiled in\n");
}
