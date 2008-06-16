/*
 * Handle the memory map.
 * The functions here do the job until bootmem takes over.
 *
 *  Getting sanitize_e820_map() in sync with i386 version by applying change:
 *  -  Provisions for empty E820 memory regions (reported by certain BIOSes).
 *     Alex Achenbach <xela@slit.de>, December 2002.
 *  Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/kexec.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pfn.h>
#include <linux/pci.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/e820.h>
#include <asm/proto.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/kdebug.h>
#include <asm/trampoline.h>

/*
 * PFN of last memory page.
 */
unsigned long end_pfn;

/*
 * end_pfn only includes RAM, while max_pfn_mapped includes all e820 entries.
 * The direct mapping extends to max_pfn_mapped, so that we can directly access
 * apertures, ACPI and other tables without having to play with fixmaps.
 */
unsigned long max_pfn_mapped;

static void early_panic(char *msg)
{
	early_printk(msg);
	panic(msg);
}

/* We're not void only for x86 32-bit compat */
char *__init machine_specific_memory_setup(void)
{
	char *who = "BIOS-e820";
	int new_nr;
	/*
	 * Try to copy the BIOS-supplied E820-map.
	 *
	 * Otherwise fake a memory map; one section from 0k->640k,
	 * the next section from 1mb->appropriate_mem_k
	 */
	new_nr = boot_params.e820_entries;
	sanitize_e820_map(boot_params.e820_map,
			ARRAY_SIZE(boot_params.e820_map),
			&new_nr);
	boot_params.e820_entries = new_nr;
	if (copy_e820_map(boot_params.e820_map, boot_params.e820_entries) < 0)
		early_panic("Cannot find a valid memory map");
	printk(KERN_INFO "BIOS-provided physical RAM map:\n");
	e820_print_map(who);

	/* In case someone cares... */
	return who;
}

int __init arch_get_ram_range(int slot, u64 *addr, u64 *size)
{
	int i;

	if (slot < 0 || slot >= e820.nr_map)
		return -1;
	for (i = slot; i < e820.nr_map; i++) {
		if (e820.map[i].type != E820_RAM)
			continue;
		break;
	}
	if (i == e820.nr_map || e820.map[i].addr > (max_pfn << PAGE_SHIFT))
		return -1;
	*addr = e820.map[i].addr;
	*size = min_t(u64, e820.map[i].size + e820.map[i].addr,
		max_pfn << PAGE_SHIFT) - *addr;
	return i + 1;
}
