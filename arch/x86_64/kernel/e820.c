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

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/e820.h>
#include <asm/proto.h>
#include <asm/bootsetup.h>
#include <asm/sections.h>

struct e820map e820 __initdata;

/* 
 * PFN of last memory page.
 */
unsigned long end_pfn; 
EXPORT_SYMBOL(end_pfn);

/* 
 * end_pfn only includes RAM, while end_pfn_map includes all e820 entries.
 * The direct mapping extends to end_pfn_map, so that we can directly access
 * apertures, ACPI and other tables without having to play with fixmaps.
 */ 
unsigned long end_pfn_map; 

/* 
 * Last pfn which the user wants to use.
 */
static unsigned long __initdata end_user_pfn = MAXMEM>>PAGE_SHIFT;

extern struct resource code_resource, data_resource;

/* Check for some hardcoded bad areas that early boot is not allowed to touch */ 
static inline int bad_addr(unsigned long *addrp, unsigned long size)
{ 
	unsigned long addr = *addrp, last = addr + size; 

	/* various gunk below that needed for SMP startup */
	if (addr < 0x8000) { 
		*addrp = PAGE_ALIGN(0x8000);
		return 1; 
	}

	/* direct mapping tables of the kernel */
	if (last >= table_start<<PAGE_SHIFT && addr < table_end<<PAGE_SHIFT) { 
		*addrp = PAGE_ALIGN(table_end << PAGE_SHIFT);
		return 1;
	} 

	/* initrd */ 
#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START && last >= INITRD_START && 
	    addr < INITRD_START+INITRD_SIZE) { 
		*addrp = PAGE_ALIGN(INITRD_START + INITRD_SIZE);
		return 1;
	} 
#endif
	/* kernel code */
	if (last >= __pa_symbol(&_text) && addr < __pa_symbol(&_end)) {
		*addrp = PAGE_ALIGN(__pa_symbol(&_end));
		return 1;
	}

	if (last >= ebda_addr && addr < ebda_addr + ebda_size) {
		*addrp = PAGE_ALIGN(ebda_addr + ebda_size);
		return 1;
	}

#ifdef CONFIG_NUMA
	/* NUMA memory to node map */
	if (last >= nodemap_addr && addr < nodemap_addr + nodemap_size) {
		*addrp = nodemap_addr + nodemap_size;
		return 1;
	}
#endif
	/* XXX ramdisk image here? */ 
	return 0;
} 

/*
 * This function checks if any part of the range <start,end> is mapped
 * with type.
 */
int __meminit
e820_any_mapped(unsigned long start, unsigned long end, unsigned type)
{ 
	int i;
	for (i = 0; i < e820.nr_map; i++) { 
		struct e820entry *ei = &e820.map[i]; 
		if (type && ei->type != type) 
			continue;
		if (ei->addr >= end || ei->addr + ei->size <= start)
			continue; 
		return 1; 
	} 
	return 0;
}

/*
 * This function checks if the entire range <start,end> is mapped with type.
 *
 * Note: this function only works correct if the e820 table is sorted and
 * not-overlapping, which is the case
 */
int __init e820_all_mapped(unsigned long start, unsigned long end, unsigned type)
{
	int i;
	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		if (type && ei->type != type)
			continue;
		/* is the region (part) in overlap with the current region ?*/
		if (ei->addr >= end || ei->addr + ei->size <= start)
			continue;

		/* if the region is at the beginning of <start,end> we move
		 * start to the end of the region since it's ok until there
		 */
		if (ei->addr <= start)
			start = ei->addr + ei->size;
		/* if start is now at or beyond end, we're done, full coverage */
		if (start >= end)
			return 1; /* we're done */
	}
	return 0;
}

/* 
 * Find a free area in a specific range. 
 */ 
unsigned long __init find_e820_area(unsigned long start, unsigned long end, unsigned size) 
{ 
	int i; 
	for (i = 0; i < e820.nr_map; i++) { 
		struct e820entry *ei = &e820.map[i]; 
		unsigned long addr = ei->addr, last; 
		if (ei->type != E820_RAM) 
			continue; 
		if (addr < start) 
			addr = start;
		if (addr > ei->addr + ei->size) 
			continue; 
		while (bad_addr(&addr, size) && addr+size <= ei->addr+ei->size)
			;
		last = PAGE_ALIGN(addr) + size;
		if (last > ei->addr + ei->size)
			continue;
		if (last > end) 
			continue;
		return addr; 
	} 
	return -1UL;		
} 

/*
 * Find the highest page frame number we have available
 */
unsigned long __init e820_end_of_ram(void)
{
	unsigned long end_pfn = 0;
	end_pfn = find_max_pfn_with_active_regions();
	
	if (end_pfn > end_pfn_map) 
		end_pfn_map = end_pfn;
	if (end_pfn_map > MAXMEM>>PAGE_SHIFT)
		end_pfn_map = MAXMEM>>PAGE_SHIFT;
	if (end_pfn > end_user_pfn)
		end_pfn = end_user_pfn;
	if (end_pfn > end_pfn_map) 
		end_pfn = end_pfn_map; 

	printk("end_pfn_map = %lu\n", end_pfn_map);
	return end_pfn;	
}

/*
 * Find the hole size in the range.
 */
unsigned long __init e820_hole_size(unsigned long start, unsigned long end)
{
	unsigned long ram = 0;
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		unsigned long last, addr;

		if (ei->type != E820_RAM ||
		    ei->addr+ei->size <= start ||
		    ei->addr >= end)
			continue;

		addr = round_up(ei->addr, PAGE_SIZE);
		if (addr < start)
			addr = start;

		last = round_down(ei->addr + ei->size, PAGE_SIZE);
		if (last >= end)
			last = end;

		if (last > addr)
			ram += last - addr;
	}
	return ((end - start) - ram);
}

/*
 * Mark e820 reserved areas as busy for the resource manager.
 */
void __init e820_reserve_resources(void)
{
	int i;
	for (i = 0; i < e820.nr_map; i++) {
		struct resource *res;
		res = alloc_bootmem_low(sizeof(struct resource));
		switch (e820.map[i].type) {
		case E820_RAM:	res->name = "System RAM"; break;
		case E820_ACPI:	res->name = "ACPI Tables"; break;
		case E820_NVS:	res->name = "ACPI Non-volatile Storage"; break;
		default:	res->name = "reserved";
		}
		res->start = e820.map[i].addr;
		res->end = res->start + e820.map[i].size - 1;
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		request_resource(&iomem_resource, res);
		if (e820.map[i].type == E820_RAM) {
			/*
			 *  We don't know which RAM region contains kernel data,
			 *  so we try it repeatedly and let the resource manager
			 *  test it.
			 */
			request_resource(res, &code_resource);
			request_resource(res, &data_resource);
#ifdef CONFIG_KEXEC
			request_resource(res, &crashk_res);
#endif
		}
	}
}

/* Mark pages corresponding to given address range as nosave */
static void __init
e820_mark_nosave_range(unsigned long start, unsigned long end)
{
	unsigned long pfn, max_pfn;

	if (start >= end)
		return;

	printk("Nosave address range: %016lx - %016lx\n", start, end);
	max_pfn = end >> PAGE_SHIFT;
	for (pfn = start >> PAGE_SHIFT; pfn < max_pfn; pfn++)
		if (pfn_valid(pfn))
			SetPageNosave(pfn_to_page(pfn));
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
			e820_mark_nosave_range(paddr,
					round_up(ei->addr, PAGE_SIZE));

		paddr = round_down(ei->addr + ei->size, PAGE_SIZE);
		if (ei->type != E820_RAM)
			e820_mark_nosave_range(round_up(ei->addr, PAGE_SIZE),
					paddr);

		if (paddr >= (end_pfn << PAGE_SHIFT))
			break;
	}
}

/* Walk the e820 map and register active regions within a node */
void __init
e820_register_active_regions(int nid, unsigned long start_pfn,
							unsigned long end_pfn)
{
	int i;
	unsigned long ei_startpfn, ei_endpfn;
	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		ei_startpfn = round_up(ei->addr, PAGE_SIZE) >> PAGE_SHIFT;
		ei_endpfn = round_down(ei->addr + ei->size, PAGE_SIZE)
								>> PAGE_SHIFT;

		/* Skip map entries smaller than a page */
		if (ei_startpfn >= ei_endpfn)
			continue;

		/* Check if end_pfn_map should be updated */
		if (ei->type != E820_RAM && ei_endpfn > end_pfn_map)
			end_pfn_map = ei_endpfn;

		/* Skip if map is outside the node */
		if (ei->type != E820_RAM ||
				ei_endpfn <= start_pfn ||
				ei_startpfn >= end_pfn)
			continue;

		/* Check for overlaps */
		if (ei_startpfn < start_pfn)
			ei_startpfn = start_pfn;
		if (ei_endpfn > end_pfn)
			ei_endpfn = end_pfn;

		/* Obey end_user_pfn to save on memmap */
		if (ei_startpfn >= end_user_pfn)
			continue;
		if (ei_endpfn > end_user_pfn)
			ei_endpfn = end_user_pfn;

		add_active_range(nid, ei_startpfn, ei_endpfn);
	}
}

/* 
 * Add a memory region to the kernel e820 map.
 */ 
void __init add_memory_region(unsigned long start, unsigned long size, int type)
{
	int x = e820.nr_map;

	if (x == E820MAX) {
		printk(KERN_ERR "Ooops! Too many entries in the memory map!\n");
		return;
	}

	e820.map[x].addr = start;
	e820.map[x].size = size;
	e820.map[x].type = type;
	e820.nr_map++;
}

void __init e820_print_map(char *who)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		printk(" %s: %016Lx - %016Lx ", who,
			(unsigned long long) e820.map[i].addr,
			(unsigned long long) (e820.map[i].addr + e820.map[i].size));
		switch (e820.map[i].type) {
		case E820_RAM:	printk("(usable)\n");
				break;
		case E820_RESERVED:
				printk("(reserved)\n");
				break;
		case E820_ACPI:
				printk("(ACPI data)\n");
				break;
		case E820_NVS:
				printk("(ACPI NVS)\n");
				break;
		default:	printk("type %u\n", e820.map[i].type);
				break;
		}
	}
}

/*
 * Sanitize the BIOS e820 map.
 *
 * Some e820 responses include overlapping entries.  The following 
 * replaces the original e820 map with a new one, removing overlaps.
 *
 */
static int __init sanitize_e820_map(struct e820entry * biosmap, char * pnr_map)
{
	struct change_member {
		struct e820entry *pbios; /* pointer to original bios entry */
		unsigned long long addr; /* address for this change point */
	};
	static struct change_member change_point_list[2*E820MAX] __initdata;
	static struct change_member *change_point[2*E820MAX] __initdata;
	static struct e820entry *overlap_list[E820MAX] __initdata;
	static struct e820entry new_bios[E820MAX] __initdata;
	struct change_member *change_tmp;
	unsigned long current_type, last_type;
	unsigned long long last_addr;
	int chgidx, still_changing;
	int overlap_entries;
	int new_bios_entry;
	int old_nr, new_nr, chg_nr;
	int i;

	/*
		Visually we're performing the following (1,2,3,4 = memory types)...

		Sample memory map (w/overlaps):
		   ____22__________________
		   ______________________4_
		   ____1111________________
		   _44_____________________
		   11111111________________
		   ____________________33__
		   ___________44___________
		   __________33333_________
		   ______________22________
		   ___________________2222_
		   _________111111111______
		   _____________________11_
		   _________________4______

		Sanitized equivalent (no overlap):
		   1_______________________
		   _44_____________________
		   ___1____________________
		   ____22__________________
		   ______11________________
		   _________1______________
		   __________3_____________
		   ___________44___________
		   _____________33_________
		   _______________2________
		   ________________1_______
		   _________________4______
		   ___________________2____
		   ____________________33__
		   ______________________4_
	*/

	/* if there's only one memory region, don't bother */
	if (*pnr_map < 2)
		return -1;

	old_nr = *pnr_map;

	/* bail out if we find any unreasonable addresses in bios map */
	for (i=0; i<old_nr; i++)
		if (biosmap[i].addr + biosmap[i].size < biosmap[i].addr)
			return -1;

	/* create pointers for initial change-point information (for sorting) */
	for (i=0; i < 2*old_nr; i++)
		change_point[i] = &change_point_list[i];

	/* record all known change-points (starting and ending addresses),
	   omitting those that are for empty memory regions */
	chgidx = 0;
	for (i=0; i < old_nr; i++)	{
		if (biosmap[i].size != 0) {
			change_point[chgidx]->addr = biosmap[i].addr;
			change_point[chgidx++]->pbios = &biosmap[i];
			change_point[chgidx]->addr = biosmap[i].addr + biosmap[i].size;
			change_point[chgidx++]->pbios = &biosmap[i];
		}
	}
	chg_nr = chgidx;

	/* sort change-point list by memory addresses (low -> high) */
	still_changing = 1;
	while (still_changing)	{
		still_changing = 0;
		for (i=1; i < chg_nr; i++)  {
			/* if <current_addr> > <last_addr>, swap */
			/* or, if current=<start_addr> & last=<end_addr>, swap */
			if ((change_point[i]->addr < change_point[i-1]->addr) ||
				((change_point[i]->addr == change_point[i-1]->addr) &&
				 (change_point[i]->addr == change_point[i]->pbios->addr) &&
				 (change_point[i-1]->addr != change_point[i-1]->pbios->addr))
			   )
			{
				change_tmp = change_point[i];
				change_point[i] = change_point[i-1];
				change_point[i-1] = change_tmp;
				still_changing=1;
			}
		}
	}

	/* create a new bios memory map, removing overlaps */
	overlap_entries=0;	 /* number of entries in the overlap table */
	new_bios_entry=0;	 /* index for creating new bios map entries */
	last_type = 0;		 /* start with undefined memory type */
	last_addr = 0;		 /* start with 0 as last starting address */
	/* loop through change-points, determining affect on the new bios map */
	for (chgidx=0; chgidx < chg_nr; chgidx++)
	{
		/* keep track of all overlapping bios entries */
		if (change_point[chgidx]->addr == change_point[chgidx]->pbios->addr)
		{
			/* add map entry to overlap list (> 1 entry implies an overlap) */
			overlap_list[overlap_entries++]=change_point[chgidx]->pbios;
		}
		else
		{
			/* remove entry from list (order independent, so swap with last) */
			for (i=0; i<overlap_entries; i++)
			{
				if (overlap_list[i] == change_point[chgidx]->pbios)
					overlap_list[i] = overlap_list[overlap_entries-1];
			}
			overlap_entries--;
		}
		/* if there are overlapping entries, decide which "type" to use */
		/* (larger value takes precedence -- 1=usable, 2,3,4,4+=unusable) */
		current_type = 0;
		for (i=0; i<overlap_entries; i++)
			if (overlap_list[i]->type > current_type)
				current_type = overlap_list[i]->type;
		/* continue building up new bios map based on this information */
		if (current_type != last_type)	{
			if (last_type != 0)	 {
				new_bios[new_bios_entry].size =
					change_point[chgidx]->addr - last_addr;
				/* move forward only if the new size was non-zero */
				if (new_bios[new_bios_entry].size != 0)
					if (++new_bios_entry >= E820MAX)
						break; 	/* no more space left for new bios entries */
			}
			if (current_type != 0)	{
				new_bios[new_bios_entry].addr = change_point[chgidx]->addr;
				new_bios[new_bios_entry].type = current_type;
				last_addr=change_point[chgidx]->addr;
			}
			last_type = current_type;
		}
	}
	new_nr = new_bios_entry;   /* retain count for new bios entries */

	/* copy new bios mapping into original location */
	memcpy(biosmap, new_bios, new_nr*sizeof(struct e820entry));
	*pnr_map = new_nr;

	return 0;
}

/*
 * Copy the BIOS e820 map into a safe place.
 *
 * Sanity-check it while we're at it..
 *
 * If we're lucky and live on a modern system, the setup code
 * will have given us a memory map that we can use to properly
 * set up memory.  If we aren't, we'll fake a memory map.
 */
static int __init copy_e820_map(struct e820entry * biosmap, int nr_map)
{
	/* Only one memory region (or negative)? Ignore it */
	if (nr_map < 2)
		return -1;

	do {
		unsigned long start = biosmap->addr;
		unsigned long size = biosmap->size;
		unsigned long end = start + size;
		unsigned long type = biosmap->type;

		/* Overflow in 64 bits? Ignore the memory map. */
		if (start > end)
			return -1;

		add_memory_region(start, size, type);
	} while (biosmap++,--nr_map);
	return 0;
}

void early_panic(char *msg)
{
	early_printk(msg);
	panic(msg);
}

void __init setup_memory_region(void)
{
	/*
	 * Try to copy the BIOS-supplied E820-map.
	 *
	 * Otherwise fake a memory map; one section from 0k->640k,
	 * the next section from 1mb->appropriate_mem_k
	 */
	sanitize_e820_map(E820_MAP, &E820_MAP_NR);
	if (copy_e820_map(E820_MAP, E820_MAP_NR) < 0)
		early_panic("Cannot find a valid memory map");
	printk(KERN_INFO "BIOS-provided physical RAM map:\n");
	e820_print_map("BIOS-e820");
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
		/* If we are doing a crash dump, we
		 * still need to know the real mem
		 * size before original memory map is
		 * reset.
		 */
		e820_register_active_regions(0, 0, -1UL);
		saved_max_pfn = e820_end_of_ram();
		remove_all_active_ranges();
#endif
		end_pfn_map = 0;
		e820.nr_map = 0;
		userdef = 1;
		return 0;
	}

	oldp = p;
	mem_size = memparse(p, &p);
	if (p == oldp)
		return -EINVAL;
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
		printk(KERN_INFO "user-defined physical RAM map:\n");
		e820_print_map("user");
	}
}

unsigned long pci_mem_start = 0xaeedbabe;
EXPORT_SYMBOL(pci_mem_start);

/*
 * Search for the biggest gap in the low 32 bits of the e820
 * memory space.  We pass this space to PCI to assign MMIO resources
 * for hotplug or unconfigured devices in.
 * Hopefully the BIOS let enough space left.
 */
__init void e820_setup_gap(void)
{
	unsigned long gapstart, gapsize, round;
	unsigned long last;
	int i;
	int found = 0;

	last = 0x100000000ull;
	gapstart = 0x10000000;
	gapsize = 0x400000;
	i = e820.nr_map;
	while (--i >= 0) {
		unsigned long long start = e820.map[i].addr;
		unsigned long long end = start + e820.map[i].size;

		/*
		 * Since "last" is at most 4GB, we know we'll
		 * fit in 32 bits if this condition is true
		 */
		if (last > end) {
			unsigned long gap = last - end;

			if (gap > gapsize) {
				gapsize = gap;
				gapstart = end;
				found = 1;
			}
		}
		if (start < last)
			last = start;
	}

	if (!found) {
		gapstart = (end_pfn << PAGE_SHIFT) + 1024*1024;
		printk(KERN_ERR "PCI: Warning: Cannot find a gap in the 32bit address range\n"
		       KERN_ERR "PCI: Unassigned devices with 32bit resource registers may break!\n");
	}

	/*
	 * See how much we want to round up: start off with
	 * rounding to the next 1MB area.
	 */
	round = 0x100000;
	while ((gapsize >> 4) > round)
		round += round;
	/* Fun with two's complement */
	pci_mem_start = (gapstart + round) & -round;

	printk(KERN_INFO "Allocating PCI resources starting at %lx (gap: %lx:%lx)\n",
		pci_mem_start, gapstart, gapsize);
}
