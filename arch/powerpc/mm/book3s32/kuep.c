// SPDX-License-Identifier: GPL-2.0-or-later

#include <asm/kup.h>
#include <asm/smp.h>

void setup_kuep(bool disabled)
{
	if (smp_processor_id() != boot_cpuid)
		return;

	pr_info("Activating Kernel Userspace Execution Prevention\n");
}
