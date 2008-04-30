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
#include <linux/uaccess.h>
#include <linux/suspend.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/e820.h>
#include <asm/setup.h>

struct e820map e820;
struct change_member {
	struct e820entry *pbios; /* pointer to original bios entry */
	unsigned long long addr; /* address for this change point */
};
static struct change_member change_point_list[2*E820MAX] __initdata;
static struct change_member *change_point[2*E820MAX] __initdata;
static struct e820entry *overlap_list[E820MAX] __initdata;
static struct e820entry new_bios[E820MAX] __initdata;
/* For PCI or other memory-mapped resources */
unsigned long pci_mem_start = 0x10000000;
#ifdef CONFIG_PCI
EXPORT_SYMBOL(pci_mem_start);
#endif
extern int user_defined_memmap;

static struct resource system_rom_resource = {
	.name	= "System ROM",
	.start	= 0xf0000,
	.end	= 0xfffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
};

static struct resource extension_rom_resource = {
	.name	= "Extension ROM",
	.start	= 0xe0000,
	.end	= 0xeffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
};

static struct resource adapter_rom_resources[] = { {
	.name 	= "Adapter ROM",
	.start	= 0xc8000,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
} };

static struct resource video_rom_resource = {
	.name 	= "Video ROM",
	.start	= 0xc0000,
	.end	= 0xc7fff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
};

#define ROMSIGNATURE 0xaa55

static int __init romsignature(const unsigned char *rom)
{
	const unsigned short * const ptr = (const unsigned short *)rom;
	unsigned short sig;

	return probe_kernel_address(ptr, sig) == 0 && sig == ROMSIGNATURE;
}

static int __init romchecksum(const unsigned char *rom, unsigned long length)
{
	unsigned char sum, c;

	for (sum = 0; length && probe_kernel_address(rom++, c) == 0; length--)
		sum += c;
	return !length && !sum;
}

static void __init probe_roms(void)
{
	const unsigned char *rom;
	unsigned long start, length, upper;
	unsigned char c;
	int i;

	/* video rom */
	upper = adapter_rom_resources[0].start;
	for (start = video_rom_resource.start; start < upper; start += 2048) {
		rom = isa_bus_to_virt(start);
		if (!romsignature(rom))
			continue;

		video_rom_resource.start = start;

		if (probe_kernel_address(rom + 2, c) != 0)
			continue;

		/* 0 < length <= 0x7f * 512, historically */
		length = c * 512;

		/* if checksum okay, trust length byte */
		if (length && romchecksum(rom, length))
			video_rom_resource.end = start + length - 1;

		request_resource(&iomem_resource, &video_rom_resource);
		break;
	}

	start = (video_rom_resource.end + 1 + 2047) & ~2047UL;
	if (start < upper)
		start = upper;

	/* system rom */
	request_resource(&iomem_resource, &system_rom_resource);
	upper = system_rom_resource.start;

	/* check for extension rom (ignore length byte!) */
	rom = isa_bus_to_virt(extension_rom_resource.start);
	if (romsignature(rom)) {
		length = extension_rom_resource.end - extension_rom_resource.start + 1;
		if (romchecksum(rom, length)) {
			request_resource(&iomem_resource, &extension_rom_resource);
			upper = extension_rom_resource.start;
		}
	}

	/* check for adapter roms on 2k boundaries */
	for (i = 0; i < ARRAY_SIZE(adapter_rom_resources) && start < upper; start += 2048) {
		rom = isa_bus_to_virt(start);
		if (!romsignature(rom))
			continue;

		if (probe_kernel_address(rom + 2, c) != 0)
			continue;

		/* 0 < length <= 0x7f * 512, historically */
		length = c * 512;

		/* but accept any length that fits if checksum okay */
		if (!length || start + length > upper || !romchecksum(rom, length))
			continue;

		adapter_rom_resources[i].start = start;
		adapter_rom_resources[i].end = start + length - 1;
		request_resource(&iomem_resource, &adapter_rom_resources[i]);

		start = adapter_rom_resources[i++].end & ~2047UL;
	}
}

/*
 * Request address space for all standard RAM and ROM resources
 * and also for regions reported as reserved by the e820.
 */
void __init init_iomem_resources(struct resource *code_resource,
		struct resource *data_resource,
		struct resource *bss_resource)
{
	int i;

	probe_roms();
	for (i = 0; i < e820.nr_map; i++) {
		struct resource *res;
#ifndef CONFIG_RESOURCES_64BIT
		if (e820.map[i].addr + e820.map[i].size > 0x100000000ULL)
			continue;
#endif
		res = kzalloc(sizeof(struct resource), GFP_ATOMIC);
		switch (e820.map[i].type) {
		case E820_RAM:	res->name = "System RAM"; break;
		case E820_ACPI:	res->name = "ACPI Tables"; break;
		case E820_NVS:	res->name = "ACPI Non-volatile Storage"; break;
		default:	res->name = "reserved";
		}
		res->start = e820.map[i].addr;
		res->end = res->start + e820.map[i].size - 1;
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		if (request_resource(&iomem_resource, res)) {
			kfree(res);
			continue;
		}
		if (e820.map[i].type == E820_RAM) {
			/*
			 *  We don't know which RAM region contains kernel data,
			 *  so we try it repeatedly and let the resource manager
			 *  test it.
			 */
			request_resource(res, code_resource);
			request_resource(res, data_resource);
			request_resource(res, bss_resource);
#ifdef CONFIG_KEXEC
			if (crashk_res.start != crashk_res.end)
				request_resource(res, &crashk_res);
#endif
		}
	}
}

#if defined(CONFIG_PM) && defined(CONFIG_HIBERNATION)
/**
 * e820_mark_nosave_regions - Find the ranges of physical addresses that do not
 * correspond to e820 RAM areas and mark the corresponding pages as nosave for
 * hibernation.
 *
 * This function requires the e820 map to be sorted and without any
 * overlapping entries and assumes the first e820 area to be RAM.
 */
void __init e820_mark_nosave_regions(void)
{
	int i;
	unsigned long pfn;

	pfn = PFN_DOWN(e820.map[0].addr + e820.map[0].size);
	for (i = 1; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		if (pfn < PFN_UP(ei->addr))
			register_nosave_region(pfn, PFN_UP(ei->addr));

		pfn = PFN_DOWN(ei->addr + ei->size);
		if (ei->type != E820_RAM)
			register_nosave_region(PFN_UP(ei->addr), pfn);

		if (pfn >= max_low_pfn)
			break;
	}
}
#endif

void __init add_memory_region(unsigned long long start,
			      unsigned long long size, int type)
{
	int x;

	x = e820.nr_map;

	if (x == E820MAX) {
		printk(KERN_ERR "Ooops! Too many entries in the memory map!\n");
		return;
	}

	e820.map[x].addr = start;
	e820.map[x].size = size;
	e820.map[x].type = type;
	e820.nr_map++;
} /* add_memory_region */

/*
 * Sanitize the BIOS e820 map.
 *
 * Some e820 responses include overlapping entries.  The following
 * replaces the original e820 map with a new one, removing overlaps.
 *
 */
int __init sanitize_e820_map(struct e820entry * biosmap, char * pnr_map)
{
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
	if (*pnr_map < 2) {
		return -1;
	}

	old_nr = *pnr_map;

	/* bail out if we find any unreasonable addresses in bios map */
	for (i=0; i<old_nr; i++)
		if (biosmap[i].addr + biosmap[i].size < biosmap[i].addr) {
			return -1;
		}

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
	chg_nr = chgidx;    	/* true number of change-points */

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
 *
 * We check to see that the memory map contains at least 2 elements
 * before we'll use it, because the detection code in setup.S may
 * not be perfect and most every PC known to man has two memory
 * regions: one from 0 to 640k, and one from 1mb up.  (The IBM
 * thinkpad 560x, for example, does not cooperate with the memory
 * detection code.)
 */
int __init copy_e820_map(struct e820entry *biosmap, int nr_map)
{
	/* Only one memory region (or negative)? Ignore it */
	if (nr_map < 2)
		return -1;

	do {
		u64 start = biosmap->addr;
		u64 size = biosmap->size;
		u64 end = start + size;
		u32 type = biosmap->type;

		/* Overflow in 64 bits? Ignore the memory map. */
		if (start > end)
			return -1;

		add_memory_region(start, size, type);
	} while (biosmap++, --nr_map);

	return 0;
}

/*
 * Find the highest page frame number we have available
 */
void __init propagate_e820_map(void)
{
	int i;

	max_pfn = 0;

	for (i = 0; i < e820.nr_map; i++) {
		unsigned long start, end;
		/* RAM? */
		if (e820.map[i].type != E820_RAM)
			continue;
		start = PFN_UP(e820.map[i].addr);
		end = PFN_DOWN(e820.map[i].addr + e820.map[i].size);
		if (start >= end)
			continue;
		if (end > max_pfn)
			max_pfn = end;
		memory_present(0, start, end);
	}
}

/*
 * Register fully available low RAM pages with the bootmem allocator.
 */
void __init register_bootmem_low_pages(unsigned long max_low_pfn)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		unsigned long curr_pfn, last_pfn, size;
		/*
		 * Reserve usable low memory
		 */
		if (e820.map[i].type != E820_RAM)
			continue;
		/*
		 * We are rounding up the start address of usable memory:
		 */
		curr_pfn = PFN_UP(e820.map[i].addr);
		if (curr_pfn >= max_low_pfn)
			continue;
		/*
		 * ... and at the end of the usable range downwards:
		 */
		last_pfn = PFN_DOWN(e820.map[i].addr + e820.map[i].size);

		if (last_pfn > max_low_pfn)
			last_pfn = max_low_pfn;

		/*
		 * .. finally, did all the rounding and playing
		 * around just make the area go away?
		 */
		if (last_pfn <= curr_pfn)
			continue;

		size = last_pfn - curr_pfn;
		free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(size));
	}
}

void __init e820_register_memory(void)
{
	unsigned long gapstart, gapsize, round;
	unsigned long long last;
	int i;

	/*
	 * Search for the biggest gap in the low 32 bits of the e820
	 * memory space.
	 */
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
			}
		}
		if (start < last)
			last = start;
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

	printk("Allocating PCI resources starting at %08lx (gap: %08lx:%08lx)\n",
		pci_mem_start, gapstart, gapsize);
}

void __init print_memory_map(char *who)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		printk(" %s: %016Lx - %016Lx ", who,
			e820.map[i].addr,
			e820.map[i].addr + e820.map[i].size);
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

void __init limit_regions(unsigned long long size)
{
	unsigned long long current_addr;
	int i;

	print_memory_map("limit_regions start");
	for (i = 0; i < e820.nr_map; i++) {
		current_addr = e820.map[i].addr + e820.map[i].size;
		if (current_addr < size)
			continue;

		if (e820.map[i].type != E820_RAM)
			continue;

		if (e820.map[i].addr >= size) {
			/*
			 * This region starts past the end of the
			 * requested size, skip it completely.
			 */
			e820.nr_map = i;
		} else {
			e820.nr_map = i + 1;
			e820.map[i].size -= current_addr - size;
		}
		print_memory_map("limit_regions endfor");
		return;
	}
	print_memory_map("limit_regions endfunc");
}

/*
 * This function checks if any part of the range <start,end> is mapped
 * with type.
 */
int
e820_any_mapped(u64 start, u64 end, unsigned type)
{
	int i;
	for (i = 0; i < e820.nr_map; i++) {
		const struct e820entry *ei = &e820.map[i];
		if (type && ei->type != type)
			continue;
		if (ei->addr >= end || ei->addr + ei->size <= start)
			continue;
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(e820_any_mapped);

 /*
  * This function checks if the entire range <start,end> is mapped with type.
  *
  * Note: this function only works correct if the e820 table is sorted and
  * not-overlapping, which is the case
  */
int __init
e820_all_mapped(unsigned long s, unsigned long e, unsigned type)
{
	u64 start = s;
	u64 end = e;
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
		/* if start is now at or beyond end, we're done, full
		 * coverage */
		if (start >= end)
			return 1; /* we're done */
	}
	return 0;
}

static int __init parse_memmap(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (strcmp(arg, "exactmap") == 0) {
#ifdef CONFIG_CRASH_DUMP
		/* If we are doing a crash dump, we
		 * still need to know the real mem
		 * size before original memory map is
		 * reset.
		 */
		propagate_e820_map();
		saved_max_pfn = max_pfn;
#endif
		e820.nr_map = 0;
		user_defined_memmap = 1;
	} else {
		/* If the user specifies memory size, we
		 * limit the BIOS-provided memory map to
		 * that size. exactmap can be used to specify
		 * the exact map. mem=number can be used to
		 * trim the existing memory map.
		 */
		unsigned long long start_at, mem_size;

		mem_size = memparse(arg, &arg);
		if (*arg == '@') {
			start_at = memparse(arg+1, &arg);
			add_memory_region(start_at, mem_size, E820_RAM);
		} else if (*arg == '#') {
			start_at = memparse(arg+1, &arg);
			add_memory_region(start_at, mem_size, E820_ACPI);
		} else if (*arg == '$') {
			start_at = memparse(arg+1, &arg);
			add_memory_region(start_at, mem_size, E820_RESERVED);
		} else {
			limit_regions(mem_size);
			user_defined_memmap = 1;
		}
	}
	return 0;
}
early_param("memmap", parse_memmap);
void __init update_memory_range(u64 start, u64 size, unsigned old_type,
				unsigned new_type)
{
	int i;

	BUG_ON(old_type == new_type);

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		u64 final_start, final_end;
		if (ei->type != old_type)
			continue;
		/* totally covered? */
		if (ei->addr >= start && ei->size <= size) {
			ei->type = new_type;
			continue;
		}
		/* partially covered */
		final_start = max(start, ei->addr);
		final_end = min(start + size, ei->addr + ei->size);
		if (final_start >= final_end)
			continue;
		add_memory_region(final_start, final_end - final_start,
					 new_type);
	}
}
void __init update_e820(void)
{
	u8 nr_map;

	nr_map = e820.nr_map;
	if (sanitize_e820_map(e820.map, &nr_map))
		return;
	e820.nr_map = nr_map;
	printk(KERN_INFO "modified physical RAM map:\n");
	print_memory_map("modified");
}
