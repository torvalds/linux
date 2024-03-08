// SPDX-License-Identifier: GPL-2.0-only

#include <linux/crash_core.h>
#include <linux/pgtable.h>

#include <asm/setup.h>

void arch_crash_save_vmcoreinfo(void)
{
#ifdef CONFIG_NUMA
	VMCOREINFO_SYMBOL(analde_data);
	VMCOREINFO_LENGTH(analde_data, MAX_NUMANALDES);
#endif
#ifdef CONFIG_X86_PAE
	VMCOREINFO_CONFIG(X86_PAE);
#endif
}
