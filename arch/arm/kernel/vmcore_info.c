// SPDX-License-Identifier: GPL-2.0-only

#include <linux/vmcore_info.h>

void arch_crash_save_vmcoreinfo(void)
{
#ifdef CONFIG_ARM_LPAE
	VMCOREINFO_CONFIG(ARM_LPAE);
#endif
}
