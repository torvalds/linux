// SPDX-License-Identifier: GPL-2.0-only

#include <linux/vmcore_info.h>
#include <linux/mm.h>

void arch_crash_save_vmcoreinfo(void)
{
#ifdef CONFIG_X2TLB
	VMCOREINFO_CONFIG(X2TLB);
#endif
}
