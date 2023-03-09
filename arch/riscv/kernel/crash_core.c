// SPDX-License-Identifier: GPL-2.0-only

#include <linux/crash_core.h>
#include <linux/pagemap.h>

void arch_crash_save_vmcoreinfo(void)
{
	VMCOREINFO_NUMBER(VA_BITS);
	VMCOREINFO_NUMBER(phys_ram_base);

	vmcoreinfo_append_str("NUMBER(PAGE_OFFSET)=0x%lx\n", PAGE_OFFSET);
	vmcoreinfo_append_str("NUMBER(VMALLOC_START)=0x%lx\n", VMALLOC_START);
	vmcoreinfo_append_str("NUMBER(VMALLOC_END)=0x%lx\n", VMALLOC_END);
	vmcoreinfo_append_str("NUMBER(VMEMMAP_START)=0x%lx\n", VMEMMAP_START);
	vmcoreinfo_append_str("NUMBER(VMEMMAP_END)=0x%lx\n", VMEMMAP_END);
#ifdef CONFIG_64BIT
	vmcoreinfo_append_str("NUMBER(MODULES_VADDR)=0x%lx\n", MODULES_VADDR);
	vmcoreinfo_append_str("NUMBER(MODULES_END)=0x%lx\n", MODULES_END);
#endif
	vmcoreinfo_append_str("NUMBER(KERNEL_LINK_ADDR)=0x%lx\n", KERNEL_LINK_ADDR);
}
