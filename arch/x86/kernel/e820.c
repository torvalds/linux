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
#include <linux/suspend.h>
#include <linux/firmware-map.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/e820.h>
#include <asm/proto.h>
#include <asm/setup.h>
#include <asm/trampoline.h>

/*
 * The e820 map is the map that gets modified e.g. with command line parameters
 * and that is also registered with modifications in the kernel resource tree
 * with the iomem_resource as parent.
 *
 * The e820_saved is directly saved after the BIOS-provided memory map is
 * copied. It doesn't get modified afterwards. It's registered for the
 * /sys/firmware/memmap interface.
 *
 * That memory map is not modified and is used as base for kexec. The kexec'd
 * kernel should get the same memory map as the firmware provides. Then the
 * user can e.g. boot the original kernel with mem=1G while still booting the
 * next kernel with full memory.
 */
struct e820map e820;
struct e820map e820_saved;

/* For PCI or other memory-mapped resources */
unsigned long pci_mem_start = 0xaeedbabe;
#ifdef CONFIG_PCI
EXPORT_SYMBOL(pci_mem_start);
#endif

/*
 * This function checks if any part of the range <start,end> is mapped
 * with type.
 */
int
e820_any_mapped(u64 start, u64 end, unsigned type)
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
EXPORT_SYMBOL_GPL(e820_any_mapped);

/*
 * This function checks if the entire range <start,end> is mapped with type.
 *
 * Note: this function only works correct if the e820 table is sorted and
 * not-overlapping, which is the case
 */
int __init e820_all_mapped(u64 start, u64 end, unsigned type)
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
		/*
		 * if start is now at or beyond end, we're done, full
		 * coverage
		 */
		if (start >= end)
			return 1;
	}
	return 0;
}

/*
 * Add a memory region to the kernel e820 map.
 */
static void __init __e820_add_region(struct e820map *e820x, u64 start, u64 size,
					 int type)
{
	int x = e820x->nr_map;

	if (x >= ARRAY_SIZE(e820x->map)) {
		printk(KERN_ERR "Ooops! Too many entries in the memory map!\n");
		return;
	}

	e820x->map[x].addr = start;
	e820x->map[x].size = size;
	e820x->map[x].type = type;
	e820x->nr_map++;
}

void __init e820_add_region(u64 start, u64 size, int type)
{
	__e820_add_region(&e820, start, size, type);
}

static void __init e820_print_type(u32 type)
{
	switch (type) {
	case E820_RAM:
	case E820_RESERVED_KERN:
		printk(KERN_CONT "(usable)");
		break;
	case E820_RESERVED:
		printk(KERN_CONT "(reserved)");
		break;
	case E820_ACPI:
		printk(KERN_CONT "(ACPI data)");
		break;
	case E820_NVS:
		printk(KERN_CONT "(ACPI NVS)");
		break;
	case E820_UNUSABLE:
		printk(KERN_CONT "(unusable)");
		break;
	default:
		printk(KERN_CONT "type %u", type);
		break;
	}
}

void __init e820_print_map(char *who)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		printk(KERN_INFO " %s: %016Lx - %016Lx ", who,
		       (unsigned long long) e820.map[i].addr,
		       (unsigned long long)
		       (e820.map[i].addr + e820.map[i].size));
		e820_print_type(e820.map[i].type);
		printk(KERN_CONT "\n");
	}
}

/*
 * Sanitize the BIOS e820 map.
 *
 * Some e820 responses include overlapping entries. The following
 * replaces the original e820 map with a new one, removing overlaps,
 * and resolving conflicting memory types in favor of highest
 * numbered type.
 *
 * The input parameter biosmap points to an array of 'struct
 * e820entry' which on entry has elements in the range [0, *pnr_map)
 * valid, and which has space for up to max_nr_map entries.
 * On return, the resulting sanitized e820 map entries will be in
 * overwritten in the same location, starting at biosmap.
 *
 * The integer pointed to by pnr_map must be valid on entry (the
 * current number of valid entries located at biosmap) and will
 * be updated on return, with the new number of valid entries
 * (something no more than max_nr_map.)
 *
 * The return value from sanitize_e820_map() is zero if it
 * successfully 'sanitized' the map entries passed in, and is -1
 * if it did nothing, which can happen if either of (1) it was
 * only passed one map entry, or (2) any of the input map entries
 * were invalid (start + size < start, meaning that the size was
 * so big the described memory range wrapped around through zero.)
 *
 *	Visually we're performing the following
 *	(1,2,3,4 = memory types)...
 *
 *	Sample memory map (w/overlaps):
 *	   ____22__________________
 *	   ______________________4_
 *	   ____1111________________
 *	   _44_____________________
 *	   11111111________________
 *	   ____________________33__
 *	   ___________44___________
 *	   __________33333_________
 *	   ______________22________
 *	   ___________________2222_
 *	   _________111111111______
 *	   _____________________11_
 *	   _________________4______
 *
 *	Sanitized equivalent (no overlap):
 *	   1_______________________
 *	   _44_____________________
 *	   ___1____________________
 *	   ____22__________________
 *	   ______11________________
 *	   _________1______________
 *	   __________3_____________
 *	   ___________44___________
 *	   _____________33_________
 *	   _______________2________
 *	   ________________1_______
 *	   _________________4______
 *	   ___________________2____
 *	   ____________________33__
 *	   ______________________4_
 */

int __init sanitize_e820_map(struct e820entry *biosmap, int max_nr_map,
			     u32 *pnr_map)
{
	struct change_member {
		struct e820entry *pbios; /* pointer to original bios entry */
		unsigned long long addr; /* address for this change point */
	};
	static struct change_member change_point_list[2*E820_X_MAX] __initdata;
	static struct change_member *change_point[2*E820_X_MAX] __initdata;
	static struct e820entry *overlap_list[E820_X_MAX] __initdata;
	static struct e820entry new_bios[E820_X_MAX] __initdata;
	struct change_member *change_tmp;
	unsigned long current_type, last_type;
	unsigned long long last_addr;
	int chgidx, still_changing;
	int overlap_entries;
	int new_bios_entry;
	int old_nr, new_nr, chg_nr;
	int i;

	/* if there's only one memory region, don't bother */
	if (*pnr_map < 2)
		return -1;

	old_nr = *pnr_map;
	BUG_ON(old_nr > max_nr_map);

	/* bail out if we find any unreasonable addresses in bios map */
	for (i = 0; i < old_nr; i++)
		if (biosmap[i].addr + biosmap[i].size < biosmap[i].addr)
			return -1;

	/* create pointers for initial change-point information (for sorting) */
	for (i = 0; i < 2 * old_nr; i++)
		change_point[i] = &change_point_list[i];

	/* record all known change-points (starting and ending addresses),
	   omitting those that are for empty memory regions */
	chgidx = 0;
	for (i = 0; i < old_nr; i++)	{
		if (biosmap[i].size != 0) {
			change_point[chgidx]->addr = biosmap[i].addr;
			change_point[chgidx++]->pbios = &biosmap[i];
			change_point[chgidx]->addr = biosmap[i].addr +
				biosmap[i].size;
			change_point[chgidx++]->pbios = &biosmap[i];
		}
	}
	chg_nr = chgidx;

	/* sort change-point list by memory addresses (low -> high) */
	still_changing = 1;
	while (still_changing)	{
		still_changing = 0;
		for (i = 1; i < chg_nr; i++)  {
			unsigned long long curaddr, lastaddr;
			unsigned long long curpbaddr, lastpbaddr;

			curaddr = change_point[i]->addr;
			lastaddr = change_point[i - 1]->addr;
			curpbaddr = change_point[i]->pbios->addr;
			lastpbaddr = change_point[i - 1]->pbios->addr;

			/*
			 * swap entries, when:
			 *
			 * curaddr > lastaddr or
			 * curaddr == lastaddr and curaddr == curpbaddr and
			 * lastaddr != lastpbaddr
			 */
			if (curaddr < lastaddr ||
			    (curaddr == lastaddr && curaddr == curpbaddr &&
			     lastaddr != lastpbaddr)) {
				change_tmp = change_point[i];
				change_point[i] = change_point[i-1];
				change_point[i-1] = change_tmp;
				still_changing = 1;
			}
		}
	}

	/* create a new bios memory map, removing overlaps */
	overlap_entries = 0;	 /* number of entries in the overlap table */
	new_bios_entry = 0;	 /* index for creating new bios map entries */
	last_type = 0;		 /* start with undefined memory type */
	last_addr = 0;		 /* start with 0 as last starting address */

	/* loop through change-points, determining affect on the new bios map */
	for (chgidx = 0; chgidx < chg_nr; chgidx++) {
		/* keep track of all overlapping bios entries */
		if (change_point[chgidx]->addr ==
		    change_point[chgidx]->pbios->addr) {
			/*
			 * add map entry to overlap list (> 1 entry
			 * implies an overlap)
			 */
			overlap_list[overlap_entries++] =
				change_point[chgidx]->pbios;
		} else {
			/*
			 * remove entry from list (order independent,
			 * so swap with last)
			 */
			for (i = 0; i < overlap_entries; i++) {
				if (overlap_list[i] ==
				    change_point[chgidx]->pbios)
					overlap_list[i] =
						overlap_list[overlap_entries-1];
			}
			overlap_entries--;
		}
		/*
		 * if there are overlapping entries, decide which
		 * "type" to use (larger value takes precedence --
		 * 1=usable, 2,3,4,4+=unusable)
		 */
		current_type = 0;
		for (i = 0; i < overlap_entries; i++)
			if (overlap_list[i]->type > current_type)
				current_type = overlap_list[i]->type;
		/*
		 * continue building up new bios map based on this
		 * information
		 */
		if (current_type != last_type)	{
			if (last_type != 0)	 {
				new_bios[new_bios_entry].size =
					change_point[chgidx]->addr - last_addr;
				/*
				 * move forward only if the new size
				 * was non-zero
				 */
				if (new_bios[new_bios_entry].size != 0)
					/*
					 * no more space left for new
					 * bios entries ?
					 */
					if (++new_bios_entry >= max_nr_map)
						break;
			}
			if (current_type != 0)	{
				new_bios[new_bios_entry].addr =
					change_point[chgidx]->addr;
				new_bios[new_bios_entry].type = current_type;
				last_addr = change_point[chgidx]->addr;
			}
			last_type = current_type;
		}
	}
	/* retain count for new bios entries */
	new_nr = new_bios_entry;

	/* copy new bios mapping into original location */
	memcpy(biosmap, new_bios, new_nr * sizeof(struct e820entry));
	*pnr_map = new_nr;

	return 0;
}

static int __init __append_e820_map(struct e820entry *biosmap, int nr_map)
{
	while (nr_map) {
		u64 start = biosmap->addr;
		u64 size = biosmap->size;
		u64 end = start + size;
		u32 type = biosmap->type;

		/* Overflow in 64 bits? Ignore the memory map. */
		if (start > end)
			return -1;

		e820_add_region(start, size, type);

		biosmap++;
		nr_map--;
	}
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
static int __init append_e820_map(struct e820entry *biosmap, int nr_map)
{
	/* Only one memory region (or negative)? Ignore it */
	if (nr_map < 2)
		return -1;

	return __append_e820_map(biosmap, nr_map);
}

static u64 __init __e820_update_range(struct e820map *e820x, u64 start,
					u64 size, unsigned old_type,
					unsigned new_type)
{
	u64 end;
	unsigned int i;
	u64 real_updated_size = 0;

	BUG_ON(old_type == new_type);

	if (size > (ULLONG_MAX - start))
		size = ULLONG_MAX - start;

	end = start + size;
	printk(KERN_DEBUG "e820 update range: %016Lx - %016Lx ",
		       (unsigned long long) start,
		       (unsigned long long) end);
	e820_print_type(old_type);
	printk(KERN_CONT " ==> ");
	e820_print_type(new_type);
	printk(KERN_CONT "\n");

	for (i = 0; i < e820x->nr_map; i++) {
		struct e820entry *ei = &e820x->map[i];
		u64 final_start, final_end;
		u64 ei_end;

		if (ei->type != old_type)
			continue;

		ei_end = ei->addr + ei->size;
		/* totally covered by new range? */
		if (ei->addr >= start && ei_end <= end) {
			ei->type = new_type;
			real_updated_size += ei->size;
			continue;
		}

		/* new range is totally covered? */
		if (ei->addr < start && ei_end > end) {
			__e820_add_region(e820x, start, size, new_type);
			__e820_add_region(e820x, end, ei_end - end, ei->type);
			ei->size = start - ei->addr;
			real_updated_size += size;
			continue;
		}

		/* partially covered */
		final_start = max(start, ei->addr);
		final_end = min(end, ei_end);
		if (final_start >= final_end)
			continue;

		__e820_add_region(e820x, final_start, final_end - final_start,
				  new_type);

		real_updated_size += final_end - final_start;

		/*
		 * left range could be head or tail, so need to update
		 * size at first.
		 */
		ei->size -= final_end - final_start;
		if (ei->addr < final_start)
			continue;
		ei->addr = final_end;
	}
	return real_updated_size;
}

u64 __init e820_update_range(u64 start, u64 size, unsigned old_type,
			     unsigned new_type)
{
	return __e820_update_range(&e820, start, size, old_type, new_type);
}

static u64 __init e820_update_range_saved(u64 start, u64 size,
					  unsigned old_type, unsigned new_type)
{
	return __e820_update_range(&e820_saved, start, size, old_type,
				     new_type);
}

/* make e820 not cover the range */
u64 __init e820_remove_range(u64 start, u64 size, unsigned old_type,
			     int checktype)
{
	int i;
	u64 real_removed_size = 0;

	if (size > (ULLONG_MAX - start))
		size = ULLONG_MAX - start;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		u64 final_start, final_end;

		if (checktype && ei->type != old_type)
			continue;
		/* totally covered? */
		if (ei->addr >= start &&
		    (ei->addr + ei->size) <= (start + size)) {
			real_removed_size += ei->size;
			memset(ei, 0, sizeof(struct e820entry));
			continue;
		}
		/* partially covered */
		final_start = max(start, ei->addr);
		final_end = min(start + size, ei->addr + ei->size);
		if (final_start >= final_end)
			continue;
		real_removed_size += final_end - final_start;

		ei->size -= final_end - final_start;
		if (ei->addr < final_start)
			continue;
		ei->addr = final_end;
	}
	return real_removed_size;
}

void __init update_e820(void)
{
	u32 nr_map;

	nr_map = e820.nr_map;
	if (sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &nr_map))
		return;
	e820.nr_map = nr_map;
	printk(KERN_INFO "modified physical RAM map:\n");
	e820_print_map("modified");
}
static void __init update_e820_saved(void)
{
	u32 nr_map;

	nr_map = e820_saved.nr_map;
	if (sanitize_e820_map(e820_saved.map, ARRAY_SIZE(e820_saved.map), &nr_map))
		return;
	e820_saved.nr_map = nr_map;
}
#define MAX_GAP_END 0x100000000ull
/*
 * Search for a gap in the e820 memory space from start_addr to end_addr.
 */
__init int e820_search_gap(unsigned long *gapstart, unsigned long *gapsize,
		unsigned long start_addr, unsigned long long end_addr)
{
	unsigned long long last;
	int i = e820.nr_map;
	int found = 0;

	last = (end_addr && end_addr < MAX_GAP_END) ? end_addr : MAX_GAP_END;

	while (--i >= 0) {
		unsigned long long start = e820.map[i].addr;
		unsigned long long end = start + e820.map[i].size;

		if (end < start_addr)
			continue;

		/*
		 * Since "last" is at most 4GB, we know we'll
		 * fit in 32 bits if this condition is true
		 */
		if (last > end) {
			unsigned long gap = last - end;

			if (gap >= *gapsize) {
				*gapsize = gap;
				*gapstart = end;
				found = 1;
			}
		}
		if (start < last)
			last = start;
	}
	return found;
}

/*
 * Search for the biggest gap in the low 32 bits of the e820
 * memory space.  We pass this space to PCI to assign MMIO resources
 * for hotplug or unconfigured devices in.
 * Hopefully the BIOS let enough space left.
 */
__init void e820_setup_gap(void)
{
	unsigned long gapstart, gapsize;
	int found;

	gapstart = 0x10000000;
	gapsize = 0x400000;
	found  = e820_search_gap(&gapstart, &gapsize, 0, MAX_GAP_END);

#ifdef CONFIG_X86_64
	if (!found) {
		gapstart = (max_pfn << PAGE_SHIFT) + 1024*1024;
		printk(KERN_ERR
	"PCI: Warning: Cannot find a gap in the 32bit address range\n"
	"PCI: Unassigned devices with 32bit resource registers may break!\n");
	}
#endif

	/*
	 * e820_reserve_resources_late protect stolen RAM already
	 */
	pci_mem_start = gapstart;

	printk(KERN_INFO
	       "Allocating PCI resources starting at %lx (gap: %lx:%lx)\n",
	       pci_mem_start, gapstart, gapsize);
}

/**
 * Because of the size limitation of struct boot_params, only first
 * 128 E820 memory entries are passed to kernel via
 * boot_params.e820_map, others are passed via SETUP_E820_EXT node of
 * linked list of struct setup_data, which is parsed here.
 */
void __init parse_e820_ext(struct setup_data *sdata, unsigned long pa_data)
{
	u32 map_len;
	int entries;
	struct e820entry *extmap;

	entries = sdata->len / sizeof(struct e820entry);
	map_len = sdata->len + sizeof(struct setup_data);
	if (map_len > PAGE_SIZE)
		sdata = early_ioremap(pa_data, map_len);
	extmap = (struct e820entry *)(sdata->data);
	__append_e820_map(extmap, entries);
	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
	if (map_len > PAGE_SIZE)
		early_iounmap(sdata, map_len);
	printk(KERN_INFO "extended physical RAM map:\n");
	e820_print_map("extended");
}

#if defined(CONFIG_X86_64) || \
	(defined(CONFIG_X86_32) && defined(CONFIG_HIBERNATION))
/**
 * Find the ranges of physical addresses that do not correspond to
 * e820 RAM areas and mark the corresponding pages as nosave for
 * hibernation (32 bit) or software suspend and suspend to RAM (64 bit).
 *
 * This function requires the e820 map to be sorted and without any
 * overlapping entries and assumes the first e820 area to be RAM.
 */
void __init e820_mark_nosave_regions(unsigned long limit_pfn)
{
	int i;
	unsigned long pfn;

	pfn = PFN_DOWN(e820.map[0].addr + e820.map[0].size);
	for (i = 1; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		if (pfn < PFN_UP(ei->addr))
			register_nosave_region(pfn, PFN_UP(ei->addr));

		pfn = PFN_DOWN(ei->addr + ei->size);
		if (ei->type != E820_RAM && ei->type != E820_RESERVED_KERN)
			register_nosave_region(PFN_UP(ei->addr), pfn);

		if (pfn >= limit_pfn)
			break;
	}
}
#endif

#ifdef CONFIG_HIBERNATION
/**
 * Mark ACPI NVS memory region, so that we can save/restore it during
 * hibernation and the subsequent resume.
 */
static int __init e820_mark_nvs_memory(void)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		if (ei->type == E820_NVS)
			hibernate_nvs_register(ei->addr, ei->size);
	}

	return 0;
}
core_initcall(e820_mark_nvs_memory);
#endif

/*
 * Early reserved memory areas.
 */
#define MAX_EARLY_RES 20

struct early_res {
	u64 start, end;
	char name[16];
	char overlap_ok;
};
static struct early_res early_res[MAX_EARLY_RES] __initdata = {
	{ 0, PAGE_SIZE, "BIOS data page" },	/* BIOS data page */
	{}
};

static int __init find_overlapped_early(u64 start, u64 end)
{
	int i;
	struct early_res *r;

	for (i = 0; i < MAX_EARLY_RES && early_res[i].end; i++) {
		r = &early_res[i];
		if (end > r->start && start < r->end)
			break;
	}

	return i;
}

/*
 * Drop the i-th range from the early reservation map,
 * by copying any higher ranges down one over it, and
 * clearing what had been the last slot.
 */
static void __init drop_range(int i)
{
	int j;

	for (j = i + 1; j < MAX_EARLY_RES && early_res[j].end; j++)
		;

	memmove(&early_res[i], &early_res[i + 1],
	       (j - 1 - i) * sizeof(struct early_res));

	early_res[j - 1].end = 0;
}

/*
 * Split any existing ranges that:
 *  1) are marked 'overlap_ok', and
 *  2) overlap with the stated range [start, end)
 * into whatever portion (if any) of the existing range is entirely
 * below or entirely above the stated range.  Drop the portion
 * of the existing range that overlaps with the stated range,
 * which will allow the caller of this routine to then add that
 * stated range without conflicting with any existing range.
 */
static void __init drop_overlaps_that_are_ok(u64 start, u64 end)
{
	int i;
	struct early_res *r;
	u64 lower_start, lower_end;
	u64 upper_start, upper_end;
	char name[16];

	for (i = 0; i < MAX_EARLY_RES && early_res[i].end; i++) {
		r = &early_res[i];

		/* Continue past non-overlapping ranges */
		if (end <= r->start || start >= r->end)
			continue;

		/*
		 * Leave non-ok overlaps as is; let caller
		 * panic "Overlapping early reservations"
		 * when it hits this overlap.
		 */
		if (!r->overlap_ok)
			return;

		/*
		 * We have an ok overlap.  We will drop it from the early
		 * reservation map, and add back in any non-overlapping
		 * portions (lower or upper) as separate, overlap_ok,
		 * non-overlapping ranges.
		 */

		/* 1. Note any non-overlapping (lower or upper) ranges. */
		strncpy(name, r->name, sizeof(name) - 1);

		lower_start = lower_end = 0;
		upper_start = upper_end = 0;
		if (r->start < start) {
		 	lower_start = r->start;
			lower_end = start;
		}
		if (r->end > end) {
			upper_start = end;
			upper_end = r->end;
		}

		/* 2. Drop the original ok overlapping range */
		drop_range(i);

		i--;		/* resume for-loop on copied down entry */

		/* 3. Add back in any non-overlapping ranges. */
		if (lower_end)
			reserve_early_overlap_ok(lower_start, lower_end, name);
		if (upper_end)
			reserve_early_overlap_ok(upper_start, upper_end, name);
	}
}

static void __init __reserve_early(u64 start, u64 end, char *name,
						int overlap_ok)
{
	int i;
	struct early_res *r;

	i = find_overlapped_early(start, end);
	if (i >= MAX_EARLY_RES)
		panic("Too many early reservations");
	r = &early_res[i];
	if (r->end)
		panic("Overlapping early reservations "
		      "%llx-%llx %s to %llx-%llx %s\n",
		      start, end - 1, name?name:"", r->start,
		      r->end - 1, r->name);
	r->start = start;
	r->end = end;
	r->overlap_ok = overlap_ok;
	if (name)
		strncpy(r->name, name, sizeof(r->name) - 1);
}

/*
 * A few early reservtations come here.
 *
 * The 'overlap_ok' in the name of this routine does -not- mean it
 * is ok for these reservations to overlap an earlier reservation.
 * Rather it means that it is ok for subsequent reservations to
 * overlap this one.
 *
 * Use this entry point to reserve early ranges when you are doing
 * so out of "Paranoia", reserving perhaps more memory than you need,
 * just in case, and don't mind a subsequent overlapping reservation
 * that is known to be needed.
 *
 * The drop_overlaps_that_are_ok() call here isn't really needed.
 * It would be needed if we had two colliding 'overlap_ok'
 * reservations, so that the second such would not panic on the
 * overlap with the first.  We don't have any such as of this
 * writing, but might as well tolerate such if it happens in
 * the future.
 */
void __init reserve_early_overlap_ok(u64 start, u64 end, char *name)
{
	drop_overlaps_that_are_ok(start, end);
	__reserve_early(start, end, name, 1);
}

/*
 * Most early reservations come here.
 *
 * We first have drop_overlaps_that_are_ok() drop any pre-existing
 * 'overlap_ok' ranges, so that we can then reserve this memory
 * range without risk of panic'ing on an overlapping overlap_ok
 * early reservation.
 */
void __init reserve_early(u64 start, u64 end, char *name)
{
	if (start >= end)
		return;

	drop_overlaps_that_are_ok(start, end);
	__reserve_early(start, end, name, 0);
}

void __init free_early(u64 start, u64 end)
{
	struct early_res *r;
	int i;

	i = find_overlapped_early(start, end);
	r = &early_res[i];
	if (i >= MAX_EARLY_RES || r->end != end || r->start != start)
		panic("free_early on not reserved area: %llx-%llx!",
			 start, end - 1);

	drop_range(i);
}

void __init early_res_to_bootmem(u64 start, u64 end)
{
	int i, count;
	u64 final_start, final_end;

	count  = 0;
	for (i = 0; i < MAX_EARLY_RES && early_res[i].end; i++)
		count++;

	printk(KERN_INFO "(%d early reservations) ==> bootmem [%010llx - %010llx]\n",
			 count, start, end);
	for (i = 0; i < count; i++) {
		struct early_res *r = &early_res[i];
		printk(KERN_INFO "  #%d [%010llx - %010llx] %16s", i,
			r->start, r->end, r->name);
		final_start = max(start, r->start);
		final_end = min(end, r->end);
		if (final_start >= final_end) {
			printk(KERN_CONT "\n");
			continue;
		}
		printk(KERN_CONT " ==> [%010llx - %010llx]\n",
			final_start, final_end);
		reserve_bootmem_generic(final_start, final_end - final_start,
				BOOTMEM_DEFAULT);
	}
}

/* Check for already reserved areas */
static inline int __init bad_addr(u64 *addrp, u64 size, u64 align)
{
	int i;
	u64 addr = *addrp;
	int changed = 0;
	struct early_res *r;
again:
	i = find_overlapped_early(addr, addr + size);
	r = &early_res[i];
	if (i < MAX_EARLY_RES && r->end) {
		*addrp = addr = round_up(r->end, align);
		changed = 1;
		goto again;
	}
	return changed;
}

/* Check for already reserved areas */
static inline int __init bad_addr_size(u64 *addrp, u64 *sizep, u64 align)
{
	int i;
	u64 addr = *addrp, last;
	u64 size = *sizep;
	int changed = 0;
again:
	last = addr + size;
	for (i = 0; i < MAX_EARLY_RES && early_res[i].end; i++) {
		struct early_res *r = &early_res[i];
		if (last > r->start && addr < r->start) {
			size = r->start - addr;
			changed = 1;
			goto again;
		}
		if (last > r->end && addr < r->end) {
			addr = round_up(r->end, align);
			size = last - addr;
			changed = 1;
			goto again;
		}
		if (last <= r->end && addr >= r->start) {
			(*sizep)++;
			return 0;
		}
	}
	if (changed) {
		*addrp = addr;
		*sizep = size;
	}
	return changed;
}

/*
 * Find a free area with specified alignment in a specific range.
 */
u64 __init find_e820_area(u64 start, u64 end, u64 size, u64 align)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		u64 addr, last;
		u64 ei_last;

		if (ei->type != E820_RAM)
			continue;
		addr = round_up(ei->addr, align);
		ei_last = ei->addr + ei->size;
		if (addr < start)
			addr = round_up(start, align);
		if (addr >= ei_last)
			continue;
		while (bad_addr(&addr, size, align) && addr+size <= ei_last)
			;
		last = addr + size;
		if (last > ei_last)
			continue;
		if (last > end)
			continue;
		return addr;
	}
	return -1ULL;
}

/*
 * Find next free range after *start
 */
u64 __init find_e820_area_size(u64 start, u64 *sizep, u64 align)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		u64 addr, last;
		u64 ei_last;

		if (ei->type != E820_RAM)
			continue;
		addr = round_up(ei->addr, align);
		ei_last = ei->addr + ei->size;
		if (addr < start)
			addr = round_up(start, align);
		if (addr >= ei_last)
			continue;
		*sizep = ei_last - addr;
		while (bad_addr_size(&addr, sizep, align) &&
			addr + *sizep <= ei_last)
			;
		last = addr + *sizep;
		if (last > ei_last)
			continue;
		return addr;
	}

	return -1ULL;
}

/*
 * pre allocated 4k and reserved it in e820
 */
u64 __init early_reserve_e820(u64 startt, u64 sizet, u64 align)
{
	u64 size = 0;
	u64 addr;
	u64 start;

	for (start = startt; ; start += size) {
		start = find_e820_area_size(start, &size, align);
		if (!(start + 1))
			return 0;
		if (size >= sizet)
			break;
	}

#ifdef CONFIG_X86_32
	if (start >= MAXMEM)
		return 0;
	if (start + size > MAXMEM)
		size = MAXMEM - start;
#endif

	addr = round_down(start + size - sizet, align);
	if (addr < start)
		return 0;
	e820_update_range(addr, sizet, E820_RAM, E820_RESERVED);
	e820_update_range_saved(addr, sizet, E820_RAM, E820_RESERVED);
	printk(KERN_INFO "update e820 for early_reserve_e820\n");
	update_e820();
	update_e820_saved();

	return addr;
}

#ifdef CONFIG_X86_32
# ifdef CONFIG_X86_PAE
#  define MAX_ARCH_PFN		(1ULL<<(36-PAGE_SHIFT))
# else
#  define MAX_ARCH_PFN		(1ULL<<(32-PAGE_SHIFT))
# endif
#else /* CONFIG_X86_32 */
# define MAX_ARCH_PFN MAXMEM>>PAGE_SHIFT
#endif

/*
 * Find the highest page frame number we have available
 */
static unsigned long __init e820_end_pfn(unsigned long limit_pfn, unsigned type)
{
	int i;
	unsigned long last_pfn = 0;
	unsigned long max_arch_pfn = MAX_ARCH_PFN;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		unsigned long start_pfn;
		unsigned long end_pfn;

		if (ei->type != type)
			continue;

		start_pfn = ei->addr >> PAGE_SHIFT;
		end_pfn = (ei->addr + ei->size) >> PAGE_SHIFT;

		if (start_pfn >= limit_pfn)
			continue;
		if (end_pfn > limit_pfn) {
			last_pfn = limit_pfn;
			break;
		}
		if (end_pfn > last_pfn)
			last_pfn = end_pfn;
	}

	if (last_pfn > max_arch_pfn)
		last_pfn = max_arch_pfn;

	printk(KERN_INFO "last_pfn = %#lx max_arch_pfn = %#lx\n",
			 last_pfn, max_arch_pfn);
	return last_pfn;
}
unsigned long __init e820_end_of_ram_pfn(void)
{
	return e820_end_pfn(MAX_ARCH_PFN, E820_RAM);
}

unsigned long __init e820_end_of_low_ram_pfn(void)
{
	return e820_end_pfn(1UL<<(32 - PAGE_SHIFT), E820_RAM);
}
/*
 * Finds an active region in the address range from start_pfn to last_pfn and
 * returns its range in ei_startpfn and ei_endpfn for the e820 entry.
 */
int __init e820_find_active_region(const struct e820entry *ei,
				  unsigned long start_pfn,
				  unsigned long last_pfn,
				  unsigned long *ei_startpfn,
				  unsigned long *ei_endpfn)
{
	u64 align = PAGE_SIZE;

	*ei_startpfn = round_up(ei->addr, align) >> PAGE_SHIFT;
	*ei_endpfn = round_down(ei->addr + ei->size, align) >> PAGE_SHIFT;

	/* Skip map entries smaller than a page */
	if (*ei_startpfn >= *ei_endpfn)
		return 0;

	/* Skip if map is outside the node */
	if (ei->type != E820_RAM || *ei_endpfn <= start_pfn ||
				    *ei_startpfn >= last_pfn)
		return 0;

	/* Check for overlaps */
	if (*ei_startpfn < start_pfn)
		*ei_startpfn = start_pfn;
	if (*ei_endpfn > last_pfn)
		*ei_endpfn = last_pfn;

	return 1;
}

/* Walk the e820 map and register active regions within a node */
void __init e820_register_active_regions(int nid, unsigned long start_pfn,
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
u64 __init e820_hole_size(u64 start, u64 end)
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
	return end - start - ((u64)ram << PAGE_SHIFT);
}

static void early_panic(char *msg)
{
	early_printk(msg);
	panic(msg);
}

static int userdef __initdata;

/* "mem=nopentium" disables the 4MB page tables. */
static int __init parse_memopt(char *p)
{
	u64 mem_size;

	if (!p)
		return -EINVAL;

#ifdef CONFIG_X86_32
	if (!strcmp(p, "nopentium")) {
		setup_clear_cpu_cap(X86_FEATURE_PSE);
		return 0;
	}
#endif

	userdef = 1;
	mem_size = memparse(p, &p);
	e820_remove_range(mem_size, ULLONG_MAX - mem_size, E820_RAM, 1);

	return 0;
}
early_param("mem", parse_memopt);

static int __init parse_memmap_opt(char *p)
{
	char *oldp;
	u64 start_at, mem_size;

	if (!p)
		return -EINVAL;

	if (!strncmp(p, "exactmap", 8)) {
#ifdef CONFIG_CRASH_DUMP
		/*
		 * If we are doing a crash dump, we still need to know
		 * the real mem size before original memory map is
		 * reset.
		 */
		saved_max_pfn = e820_end_of_ram_pfn();
#endif
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
		e820_add_region(start_at, mem_size, E820_RAM);
	} else if (*p == '#') {
		start_at = memparse(p+1, &p);
		e820_add_region(start_at, mem_size, E820_ACPI);
	} else if (*p == '$') {
		start_at = memparse(p+1, &p);
		e820_add_region(start_at, mem_size, E820_RESERVED);
	} else
		e820_remove_range(mem_size, ULLONG_MAX - mem_size, E820_RAM, 1);

	return *p == '\0' ? 0 : -EINVAL;
}
early_param("memmap", parse_memmap_opt);

void __init finish_e820_parsing(void)
{
	if (userdef) {
		u32 nr = e820.nr_map;

		if (sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &nr) < 0)
			early_panic("Invalid user supplied memory map");
		e820.nr_map = nr;

		printk(KERN_INFO "user-defined physical RAM map:\n");
		e820_print_map("user");
	}
}

static inline const char *e820_type_to_string(int e820_type)
{
	switch (e820_type) {
	case E820_RESERVED_KERN:
	case E820_RAM:	return "System RAM";
	case E820_ACPI:	return "ACPI Tables";
	case E820_NVS:	return "ACPI Non-volatile Storage";
	case E820_UNUSABLE:	return "Unusable memory";
	default:	return "reserved";
	}
}

/*
 * Mark e820 reserved areas as busy for the resource manager.
 */
static struct resource __initdata *e820_res;
void __init e820_reserve_resources(void)
{
	int i;
	struct resource *res;
	u64 end;

	res = alloc_bootmem(sizeof(struct resource) * e820.nr_map);
	e820_res = res;
	for (i = 0; i < e820.nr_map; i++) {
		end = e820.map[i].addr + e820.map[i].size - 1;
		if (end != (resource_size_t)end) {
			res++;
			continue;
		}
		res->name = e820_type_to_string(e820.map[i].type);
		res->start = e820.map[i].addr;
		res->end = end;

		res->flags = IORESOURCE_MEM;

		/*
		 * don't register the region that could be conflicted with
		 * pci device BAR resource and insert them later in
		 * pcibios_resource_survey()
		 */
		if (e820.map[i].type != E820_RESERVED || res->start < (1ULL<<20)) {
			res->flags |= IORESOURCE_BUSY;
			insert_resource(&iomem_resource, res);
		}
		res++;
	}

	for (i = 0; i < e820_saved.nr_map; i++) {
		struct e820entry *entry = &e820_saved.map[i];
		firmware_map_add_early(entry->addr,
			entry->addr + entry->size - 1,
			e820_type_to_string(entry->type));
	}
}

/* How much should we pad RAM ending depending on where it is? */
static unsigned long ram_alignment(resource_size_t pos)
{
	unsigned long mb = pos >> 20;

	/* To 64kB in the first megabyte */
	if (!mb)
		return 64*1024;

	/* To 1MB in the first 16MB */
	if (mb < 16)
		return 1024*1024;

	/* To 64MB for anything above that */
	return 64*1024*1024;
}

#define MAX_RESOURCE_SIZE ((resource_size_t)-1)

void __init e820_reserve_resources_late(void)
{
	int i;
	struct resource *res;

	res = e820_res;
	for (i = 0; i < e820.nr_map; i++) {
		if (!res->parent && res->end)
			insert_resource_expand_to_fit(&iomem_resource, res);
		res++;
	}

	/*
	 * Try to bump up RAM regions to reasonable boundaries to
	 * avoid stolen RAM:
	 */
	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *entry = &e820.map[i];
		u64 start, end;

		if (entry->type != E820_RAM)
			continue;
		start = entry->addr + entry->size;
		end = round_up(start, ram_alignment(start)) - 1;
		if (end > MAX_RESOURCE_SIZE)
			end = MAX_RESOURCE_SIZE;
		if (start >= end)
			continue;
		reserve_region_with_split(&iomem_resource, start, end,
					  "RAM buffer");
	}
}

char *__init default_machine_specific_memory_setup(void)
{
	char *who = "BIOS-e820";
	u32 new_nr;
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
	if (append_e820_map(boot_params.e820_map, boot_params.e820_entries)
	  < 0) {
		u64 mem_size;

		/* compare results from other methods and take the greater */
		if (boot_params.alt_mem_k
		    < boot_params.screen_info.ext_mem_k) {
			mem_size = boot_params.screen_info.ext_mem_k;
			who = "BIOS-88";
		} else {
			mem_size = boot_params.alt_mem_k;
			who = "BIOS-e801";
		}

		e820.nr_map = 0;
		e820_add_region(0, LOWMEMSIZE(), E820_RAM);
		e820_add_region(HIGH_MEMORY, mem_size << 10, E820_RAM);
	}

	/* In case someone cares... */
	return who;
}

void __init setup_memory_map(void)
{
	char *who;

	who = x86_init.resources.memory_setup();
	memcpy(&e820_saved, &e820, sizeof(struct e820map));
	printk(KERN_INFO "BIOS-provided physical RAM map:\n");
	e820_print_map(who);
}
