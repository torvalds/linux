// SPDX-License-Identifier: GPL-2.0

#include <linux/vmcore_info.h>
#include <asm/addrspace.h>
#include <asm/pgtable.h>
#include <asm/setup.h>

void arch_crash_save_vmcoreinfo(void)
{
	VMCOREINFO_NUMBER(VA_BITS);
	/* NUMBER(VMALLOC_START) is exported by the generic code */
	vmcoreinfo_append_str("NUMBER(VMALLOC_END)=0x%lx\n", VMALLOC_END);
#ifdef CONFIG_64BIT
	vmcoreinfo_append_str("NUMBER(MODULES_VADDR)=0x%lx\n", MODULES_VADDR);
	vmcoreinfo_append_str("NUMBER(MODULES_END)=0x%lx\n", MODULES_END);
	vmcoreinfo_append_str("NUMBER(VMEMMAP_START)=0x%lx\n", (unsigned long)vmemmap);
	vmcoreinfo_append_str("NUMBER(VMEMMAP_END)=0x%lx\n", VMEMMAP_END + 1);
#endif
	vmcoreinfo_append_str("NUMBER(PHYS_OFFSET)=0x%lx\n", PHYS_OFFSET);
	vmcoreinfo_append_str("NUMBER(PAGE_OFFSET)=0x%lx\n", PAGE_OFFSET);
	vmcoreinfo_append_str("KERNELOFFSET=%lx\n", kaslr_offset());
}
