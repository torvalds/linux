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
#include <linux/suspend.h>
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

/*
 * Last pfn which the user wants to use.
 */
static unsigned long __initdata end_user_pfn = MAXMEM>>PAGE_SHIFT;

/*
 * Find the highest page frame number we have available
 */
unsigned long __init e820_end_of_ram(void)
{
	unsigned long last_pfn;

	last_pfn = find_max_pfn_with_active_regions();

	if (last_pfn > max_pfn_mapped)
		max_pfn_mapped = last_pfn;
	if (max_pfn_mapped > MAXMEM>>PAGE_SHIFT)
		max_pfn_mapped = MAXMEM>>PAGE_SHIFT;
	if (last_pfn > end_user_pfn)
		last_pfn = end_user_pfn;
	if (last_pfn > max_pfn_mapped)
		last_pfn = max_pfn_mapped;

	printk(KERN_INFO "max_pfn_mapped = %lu\n", max_pfn_mapped);
	return last_pfn;
}

/*
 * Mark e820 reserved areas as busy for the resource manager.
 */
void __init e820_reserve_resources(void)
{
	int i;
	struct resource *res;

	res = alloc_bootmem_low(sizeof(struct resource) * e820.nr_map);
	for (i = 0; i < e820.nr_map; i++) {
		switch (e820.map[i].type) {
		case E820_RAM:	res->name = "System RAM"; break;
		case E820_ACPI:	res->name = "ACPI Tables"; break;
		case E820_NVS:	res->name = "ACPI Non-volatile Storage"; break;
		default:	res->name = "reserved";
		}
		res->start = e820.map[i].addr;
		res->end = res->start + e820.map[i].size - 1;
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		insert_resource(&iomem_resource, res);
		res++;
	}
}

/*
 * Find the ranges of physical addresses that do not correspond to
 * e820 RAM areas and mark the corresponding pages as nosave for software
 * suspend and suspend to RAM.
 *
 * This function requires the e820 map to be sorted and without any
 * overlapping entries and assumes the first e820 area to be RAM.
 */
void __init e820_mark_nosave_regions(void)
{
	int i;
	unsigned long paddr;

	paddr = round_down(e820.map[0].addr + e820.map[0].size, PAGE_SIZE);
	for (i = 1; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		if (paddr < ei->addr)
			register_nosave_region(PFN_DOWN(paddr),
						PFN_UP(ei->addr));

		paddr = round_down(ei->addr + ei->size, PAGE_SIZE);
		if (ei->type != E820_RAM)
			register_nosave_region(PFN_UP(ei->addr),
						PFN_DOWN(paddr));

		if (paddr >= (end_pfn << PAGE_SHIFT))
			break;
	}
}

/*
 * Finds an active region in the address range from start_pfn to last_pfn and
 * returns its range in ei_startpfn and ei_endpfn for the e820 entry.
 */
static int __init e820_find_active_region(const struct e820entry *ei,
					  unsigned long start_pfn,
					  unsigned long last_pfn,
					  unsigned long *ei_startpfn,
					  unsigned long *ei_endpfn)
{
	*ei_startpfn = round_up(ei->addr, PAGE_SIZE) >> PAGE_SHIFT;
	*ei_endpfn = round_down(ei->addr + ei->size, PAGE_SIZE) >> PAGE_SHIFT;

	/* Skip map entries smaller than a page */
	if (*ei_startpfn >= *ei_endpfn)
		return 0;

	/* Check if max_pfn_mapped should be updated */
	if (ei->type != E820_RAM && *ei_endpfn > max_pfn_mapped)
		max_pfn_mapped = *ei_endpfn;

	/* Skip if map is outside the node */
	if (ei->type != E820_RAM || *ei_endpfn <= start_pfn ||
				    *ei_startpfn >= last_pfn)
		return 0;

	/* Check for overlaps */
	if (*ei_startpfn < start_pfn)
		*ei_startpfn = start_pfn;
	if (*ei_endpfn > last_pfn)
		*ei_endpfn = last_pfn;

	/* Obey end_user_pfn to save on memmap */
	if (*ei_startpfn >= end_user_pfn)
		return 0;
	if (*ei_endpfn > end_user_pfn)
		*ei_endpfn = end_user_pfn;

	return 1;
}

/* Walk the e820 map and register active regions within a node */
void __init
e820_register_active_regions(int nid, unsigned long start_pfn,
							unsigned long last_pfn)
{
	unsigned long ei_startpfn;
	unsigned long ei_endpfn;
	int i;

	for (i = 0; i < e820.nr_map; i++)
		if (e820_find_active_region(&e820.map[i],
					    start_pfn, last_pfn,
					    &ei_startpfn, &ei_endpfn))
			add_active_range(nid, ei_startpfn, ei_endpfn);
}

/*
 * Find the hole size (in bytes) in the memory range.
 * @start: starting address of the memory range to scan
 * @end: ending address of the memory range to scan
 */
unsigned long __init e820_hole_size(unsigned long start, unsigned long end)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long last_pfn = end >> PAGE_SHIFT;
	unsigned long ei_startpfn, ei_endpfn, ram = 0;
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		if (e820_find_active_region(&e820.map[i],
					    start_pfn, last_pfn,
					    &ei_startpfn, &ei_endpfn))
			ram += ei_endpfn - ei_startpfn;
	}
	return end - start - (ram << PAGE_SHIFT);
}

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

static int __init parse_memopt(char *p)
{
	if (!p)
		return -EINVAL;
	end_user_pfn = memparse(p, &p);
	end_user_pfn >>= PAGE_SHIFT;
	return 0;
}
early_param("mem", parse_memopt);

static int userdef __initdata;

static int __init parse_memmap_opt(char *p)
{
	char *oldp;
	unsigned long long start_at, mem_size;

	if (!strcmp(p, "exactmap")) {
#ifdef CONFIG_CRASH_DUMP
		/*
		 * If we are doing a crash dump, we still need to know
		 * the real mem size before original memory map is
		 * reset.
		 */
		e820_register_active_regions(0, 0, -1UL);
		saved_max_pfn = e820_end_of_ram();
		remove_all_active_ranges();
#endif
		max_pfn_mapped = 0;
		e820.nr_map = 0;
		userdef = 1;
		return 0;
	}

	oldp = p;
	mem_size = memparse(p, &p);
	if (p == oldp)
		return -EINVAL;

	userdef = 1;
	if (*p == '@') {
		start_at = memparse(p+1, &p);
		add_memory_region(start_at, mem_size, E820_RAM);
	} else if (*p == '#') {
		start_at = memparse(p+1, &p);
		add_memory_region(start_at, mem_size, E820_ACPI);
	} else if (*p == '$') {
		start_at = memparse(p+1, &p);
		add_memory_region(start_at, mem_size, E820_RESERVED);
	} else {
		end_user_pfn = (mem_size >> PAGE_SHIFT);
	}
	return *p == '\0' ? 0 : -EINVAL;
}
early_param("memmap", parse_memmap_opt);

void __init finish_e820_parsing(void)
{
	if (userdef) {
		int nr = e820.nr_map;

		if (sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &nr) < 0)
			early_panic("Invalid user supplied memory map");
		e820.nr_map = nr;

		printk(KERN_INFO "user-defined physical RAM map:\n");
		e820_print_map("user");
	}
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
