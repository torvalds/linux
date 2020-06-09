// SPDX-License-Identifier: GPL-2.0-only

#include <linux/crash_core.h>
#include <linux/pgtable.h>

#include <asm/setup.h>

void arch_crash_save_vmcoreinfo(void)
{
	u64 sme_mask = sme_me_mask;

	VMCOREINFO_NUMBER(phys_base);
	VMCOREINFO_SYMBOL(init_top_pgt);
	vmcoreinfo_append_str("NUMBER(pgtable_l5_enabled)=%d\n",
			      pgtable_l5_enabled());

#ifdef CONFIG_NUMA
	VMCOREINFO_SYMBOL(node_data);
	VMCOREINFO_LENGTH(node_data, MAX_NUMNODES);
#endif
	vmcoreinfo_append_str("KERNELOFFSET=%lx\n", kaslr_offset());
	VMCOREINFO_NUMBER(KERNEL_IMAGE_SIZE);
	VMCOREINFO_NUMBER(sme_mask);
}
