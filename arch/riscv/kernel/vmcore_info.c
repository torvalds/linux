// SPDX-License-Identifier: GPL-2.0-only

#include <linux/vmcore_info.h>
#include <linux/pagemap.h>

void arch_crash_save_vmcoreinfo(void)
{
	VMCOREINFO_NUMBER(phys_ram_base);

	vmcoreinfo_append_str("NUMBER(PAGE_OFFSET)=0x%lx\n", PAGE_OFFSET);
	vmcoreinfo_append_str("NUMBER(VMALLOC_END)=0x%lx\n", VMALLOC_END);
#ifdef CONFIG_MMU
	VMCOREINFO_NUMBER(VA_BITS);
	vmcoreinfo_append_str("NUMBER(VMEMMAP_START)=0x%lx\n", VMEMMAP_START);
	vmcoreinfo_append_str("NUMBER(VMEMMAP_END)=0x%lx\n", VMEMMAP_END);
#ifdef CONFIG_64BIT
	vmcoreinfo_append_str("NUMBER(MODULES_VADDR)=0x%lx\n", MODULES_VADDR);
	vmcoreinfo_append_str("NUMBER(MODULES_END)=0x%lx\n", MODULES_END);
#endif
#endif
	vmcoreinfo_append_str("NUMBER(KERNEL_LINK_ADDR)=0x%lx\n", KERNEL_LINK_ADDR);
#ifdef CONFIG_XIP_KERNEL
	/* TODO: Communicate with crash-utility developers on the information to
	 * export. The XIP case is more complicated, because the virtual-physical
	 * address offset depends on whether the address is in ROM or in RAM.
	 */
#else
	vmcoreinfo_append_str("NUMBER(va_kernel_pa_offset)=0x%lx\n",
						kernel_map.va_kernel_pa_offset);
#endif
}
