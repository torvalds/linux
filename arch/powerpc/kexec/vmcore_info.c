// SPDX-License-Identifier: GPL-2.0-only

#include <linux/vmcore_info.h>
#include <asm/pgalloc.h>

void arch_crash_save_vmcoreinfo(void)
{

#ifdef CONFIG_NUMA
	VMCOREINFO_SYMBOL(node_data);
	VMCOREINFO_LENGTH(node_data, MAX_NUMNODES);
#endif
#ifndef CONFIG_NUMA
	VMCOREINFO_SYMBOL(contig_page_data);
#endif
#if defined(CONFIG_PPC64) && defined(CONFIG_SPARSEMEM_VMEMMAP)
	VMCOREINFO_SYMBOL(vmemmap_list);
	VMCOREINFO_SYMBOL(mmu_vmemmap_psize);
	VMCOREINFO_SYMBOL(mmu_psize_defs);
	VMCOREINFO_STRUCT_SIZE(vmemmap_backing);
	VMCOREINFO_OFFSET(vmemmap_backing, list);
	VMCOREINFO_OFFSET(vmemmap_backing, phys);
	VMCOREINFO_OFFSET(vmemmap_backing, virt_addr);
	VMCOREINFO_STRUCT_SIZE(mmu_psize_def);
	VMCOREINFO_OFFSET(mmu_psize_def, shift);
#endif
	VMCOREINFO_SYMBOL(cur_cpu_spec);
	VMCOREINFO_OFFSET(cpu_spec, cpu_features);
	VMCOREINFO_OFFSET(cpu_spec, mmu_features);
	vmcoreinfo_append_str("NUMBER(RADIX_MMU)=%d\n", early_radix_enabled());
	vmcoreinfo_append_str("KERNELOFFSET=%lx\n", kaslr_offset());
}
