// SPDX-License-Identifier: GPL-2.0-only
/*
 * Low level x86 E820 memory map handling functions.
 *
 * The firmware and bootloader passes us the "E820 table", which is the primary
 * physical memory layout description available about x86 systems.
 *
 * The kernel takes the E820 memory layout and optionally modifies it with
 * quirks and other tweaks, and feeds that into the generic Linux memory
 * allocation code routines via a platform independent interface (memblock, etc.).
 */
#include <linux/memblock.h>
#include <linux/suspend.h>
#include <linux/acpi.h>
#include <linux/firmware-map.h>
#include <linux/sort.h>
#include <linux/kvm_types.h>

#include <asm/e820/api.h>
#include <asm/setup.h>

/*
 * We organize the E820 table into three main data structures:
 *
 * - 'e820_table_firmware': the original firmware version passed to us by the
 *   bootloader - not modified by the kernel. It is composed of two parts:
 *   the first 128 E820 memory entries in boot_params.e820_table and the remaining
 *   (if any) entries of the SETUP_E820_EXT nodes. We use this to:
 *
 *       - the hibernation code uses it to generate a kernel-independent CRC32
 *         checksum of the physical memory layout of a system.
 *
 * - 'e820_table_kexec': a slightly modified (by the kernel) firmware version
 *   passed to us by the bootloader - the major difference between
 *   e820_table_firmware[] and this one is that e820_table_kexec[]
 *   might be modified by the kexec itself to fake an mptable.
 *   We use this to:
 *
 *       - kexec, which is a bootloader in disguise, uses the original E820
 *         layout to pass to the kexec-ed kernel. This way the original kernel
 *         can have a restricted E820 map while the kexec()-ed kexec-kernel
 *         can have access to full memory - etc.
 *
 *         Export the memory layout via /sys/firmware/memmap. kexec-tools uses
 *         the entries to create an E820 table for the kexec kernel.
 *
 *         kexec_file_load in-kernel code uses the table for the kexec kernel.
 *
 * - 'e820_table': this is the main E820 table that is massaged by the
 *   low level x86 platform code, or modified by boot parameters, before
 *   passed on to higher level MM layers.
 *
 * Once the E820 map has been converted to the standard Linux memory layout
 * information its role stops - modifying it has no effect and does not get
 * re-propagated. So its main role is a temporary bootstrap storage of firmware
 * specific memory layout data during early bootup.
 */
__initdata static struct e820_table e820_table_init;
__initdata static struct e820_table e820_table_kexec_init;
__initdata static struct e820_table e820_table_firmware_init;

__refdata struct e820_table *e820_table			= &e820_table_init;
__refdata struct e820_table *e820_table_kexec		= &e820_table_kexec_init;
__refdata struct e820_table *e820_table_firmware	= &e820_table_firmware_init;

/* For PCI or other memory-mapped resources */
unsigned long pci_mem_start = 0xaeedbabe;
#ifdef CONFIG_PCI
EXPORT_SYMBOL(pci_mem_start);
#endif

/*
 * This function checks if any part of the range <start,end> is mapped
 * with type.
 */
static bool _e820__mapped_any(struct e820_table *table,
			      u64 start, u64 end, enum e820_type type)
{
	u32 idx;

	for (idx = 0; idx < table->nr_entries; idx++) {
		struct e820_entry *entry = &table->entries[idx];

		if (type && entry->type != type)
			continue;
		if (entry->addr >= end || entry->addr + entry->size <= start)
			continue;
		return true;
	}
	return false;
}

bool e820__mapped_raw_any(u64 start, u64 end, enum e820_type type)
{
	return _e820__mapped_any(e820_table_firmware, start, end, type);
}
EXPORT_SYMBOL_FOR_KVM(e820__mapped_raw_any);

bool e820__mapped_any(u64 start, u64 end, enum e820_type type)
{
	return _e820__mapped_any(e820_table, start, end, type);
}
EXPORT_SYMBOL_GPL(e820__mapped_any);

/*
 * This function checks if the entire <start,end> range is mapped with 'type'.
 *
 * Note: this function only works correctly once the E820 table is sorted and
 * not-overlapping (at least for the range specified), which is the case normally.
 */
static struct e820_entry *__e820__mapped_all(u64 start, u64 end,
					     enum e820_type type)
{
	u32 idx;

	for (idx = 0; idx < e820_table->nr_entries; idx++) {
		struct e820_entry *entry = &e820_table->entries[idx];

		if (type && entry->type != type)
			continue;

		/* Is the region (part) in overlap with the current region? */
		if (entry->addr >= end || entry->addr + entry->size <= start)
			continue;

		/*
		 * If the region is at the beginning of <start,end> we move
		 * 'start' to the end of the region since it's ok until there
		 */
		if (entry->addr <= start)
			start = entry->addr + entry->size;

		/*
		 * If 'start' is now at or beyond 'end', we're done, full
		 * coverage of the desired range exists:
		 */
		if (start >= end)
			return entry;
	}

	return NULL;
}

/*
 * This function checks if the entire range <start,end> is mapped with type.
 */
__init bool e820__mapped_all(u64 start, u64 end, enum e820_type type)
{
	return __e820__mapped_all(start, end, type);
}

/*
 * This function returns the type associated with the range <start,end>.
 */
int e820__get_entry_type(u64 start, u64 end)
{
	struct e820_entry *entry = __e820__mapped_all(start, end, 0);

	return entry ? entry->type : -EINVAL;
}

/*
 * Add a memory region to the kernel E820 map.
 */
__init static void __e820__range_add(struct e820_table *table, u64 start, u64 size, enum e820_type type)
{
	u32 idx = table->nr_entries;
	struct e820_entry *entry_new;

	if (idx >= ARRAY_SIZE(table->entries)) {
		pr_err("E820 table full; ignoring [mem %#010llx-%#010llx]\n",
		       start, start + size-1);
		return;
	}

	entry_new = table->entries + idx;

	entry_new->addr = start;
	entry_new->size = size;
	entry_new->type = type;

	table->nr_entries++;
}

__init void e820__range_add(u64 start, u64 size, enum e820_type type)
{
	__e820__range_add(e820_table, start, size, type);
}

__init static void e820_print_type(enum e820_type type)
{
	switch (type) {
	case E820_TYPE_RAM:		pr_cont(" System RAM");				break;
	case E820_TYPE_RESERVED:	pr_cont(" device reserved");			break;
	case E820_TYPE_SOFT_RESERVED:	pr_cont(" soft reserved");			break;
	case E820_TYPE_ACPI:		pr_cont(" ACPI data");				break;
	case E820_TYPE_NVS:		pr_cont(" ACPI NVS");				break;
	case E820_TYPE_UNUSABLE:	pr_cont(" unusable");				break;
	case E820_TYPE_PMEM:		/* Fall through: */
	case E820_TYPE_PRAM:		pr_cont(" persistent RAM (type %u)", type);	break;
	default:			pr_cont(" type %u", type);			break;
	}
}

__init static void e820__print_table(const char *who)
{
	u64 range_end_prev = 0;
	u32 idx;

	for (idx = 0; idx < e820_table->nr_entries; idx++) {
		struct e820_entry *entry = e820_table->entries + idx;
		u64 range_start, range_end;

		range_start = entry->addr;
		range_end   = entry->addr + entry->size;

		/* Out of order E820 maps should not happen: */
		if (range_start < range_end_prev)
			pr_info(FW_BUG "out of order E820 entry!\n");

		if (range_start > range_end_prev) {
			pr_info("%s: [gap %#018Lx-%#018Lx]\n",
				who,
				range_end_prev,
				range_start-1);
		}

		pr_info("%s: [mem %#018Lx-%#018Lx] ", who, range_start, range_end-1);
		e820_print_type(entry->type);
		pr_cont("\n");

		range_end_prev = range_end;
	}
}

/*
 * Sanitize an E820 map.
 *
 * Some E820 layouts include overlapping entries. The following
 * replaces the original E820 map with a new one, removing overlaps,
 * and resolving conflicting memory types in favor of highest
 * numbered type.
 *
 * The input parameter 'entries' points to an array of 'struct
 * e820_entry' which on entry has elements in the range [0, *nr_entries)
 * valid, and which has space for up to max_nr_entries entries.
 * On return, the resulting sanitized E820 map entries will be in
 * overwritten in the same location, starting at 'entries'.
 *
 * The integer pointed to by nr_entries must be valid on entry (the
 * current number of valid entries located at 'entries'). If the
 * sanitizing succeeds the *nr_entries will be updated with the new
 * number of valid entries (something no more than max_nr_entries).
 *
 * The return value from e820__update_table() is zero if it
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
struct change_member {
	/* Pointer to the original entry: */
	struct e820_entry	*entry;
	/* Address for this change point: */
	u64			addr;
};

__initdata static struct change_member	change_point_list[2*E820_MAX_ENTRIES];
__initdata static struct change_member	*change_point[2*E820_MAX_ENTRIES];
__initdata static struct e820_entry	*overlap_list[E820_MAX_ENTRIES];
__initdata static struct e820_entry	new_entries[E820_MAX_ENTRIES];

__init static int cpcompare(const void *a, const void *b)
{
	struct change_member * const *app = a, * const *bpp = b;
	const struct change_member *ap = *app, *bp = *bpp;

	/*
	 * Inputs are pointers to two elements of change_point[].  If their
	 * addresses are not equal, their difference dominates.  If the addresses
	 * are equal, then consider one that represents the end of its region
	 * to be greater than one that does not.
	 */
	if (ap->addr != bp->addr)
		return ap->addr > bp->addr ? 1 : -1;

	return (ap->addr != ap->entry->addr) - (bp->addr != bp->entry->addr);
}

/*
 * Can two consecutive E820 entries of this same E820 type be merged?
 */
static bool e820_type_mergeable(enum e820_type type)
{
	/*
	 * These types may indicate distinct platform ranges aligned to
	 * NUMA node, protection domain, performance domain, or other
	 * boundaries. Do not merge them.
	 */
	if (type == E820_TYPE_PRAM)
		return false;
	if (type == E820_TYPE_SOFT_RESERVED)
		return false;

	return true;
}

__init int e820__update_table(struct e820_table *table)
{
	struct e820_entry *entries = table->entries;
	u32 max_nr_entries = ARRAY_SIZE(table->entries);
	enum e820_type current_type, last_type;
	u64 last_addr;
	u32 new_nr_entries, overlap_entries;
	u32 idx, chg_idx, chg_nr;

	/* If there's only one memory region, don't bother: */
	if (table->nr_entries < 2)
		return -1;

	BUG_ON(table->nr_entries > max_nr_entries);

	/* Bail out if we find any unreasonable addresses in the map: */
	for (idx = 0; idx < table->nr_entries; idx++) {
		if (entries[idx].addr + entries[idx].size < entries[idx].addr)
			return -1;
	}

	/* Create pointers for initial change-point information (for sorting): */
	for (idx = 0; idx < 2 * table->nr_entries; idx++)
		change_point[idx] = &change_point_list[idx];

	/*
	 * Record all known change-points (starting and ending addresses),
	 * omitting empty memory regions:
	 */
	chg_idx = 0;
	for (idx = 0; idx < table->nr_entries; idx++)	{
		if (entries[idx].size != 0) {
			change_point[chg_idx]->addr	= entries[idx].addr;
			change_point[chg_idx++]->entry	= &entries[idx];
			change_point[chg_idx]->addr	= entries[idx].addr + entries[idx].size;
			change_point[chg_idx++]->entry	= &entries[idx];
		}
	}
	chg_nr = chg_idx;

	/* Sort change-point list by memory addresses (low -> high): */
	sort(change_point, chg_nr, sizeof(*change_point), cpcompare, NULL);

	/* Create a new memory map, removing overlaps: */
	overlap_entries = 0;	 /* Number of entries in the overlap table */
	new_nr_entries = 0;	 /* Index for creating new map entries */
	last_type = 0;		 /* Start with undefined memory type */
	last_addr = 0;		 /* Start with 0 as last starting address */

	/* Loop through change-points, determining effect on the new map: */
	for (chg_idx = 0; chg_idx < chg_nr; chg_idx++) {
		/* Keep track of all overlapping entries */
		if (change_point[chg_idx]->addr == change_point[chg_idx]->entry->addr) {
			/* Add map entry to overlap list (> 1 entry implies an overlap) */
			overlap_list[overlap_entries++] = change_point[chg_idx]->entry;
		} else {
			/* Remove entry from list (order independent, so swap with last): */
			for (idx = 0; idx < overlap_entries; idx++) {
				if (overlap_list[idx] == change_point[chg_idx]->entry)
					overlap_list[idx] = overlap_list[overlap_entries-1];
			}
			overlap_entries--;
		}
		/*
		 * If there are overlapping entries, decide which
		 * "type" to use (larger value takes precedence --
		 * 1=usable, 2,3,4,4+=unusable)
		 */
		current_type = 0;
		for (idx = 0; idx < overlap_entries; idx++) {
			if (overlap_list[idx]->type > current_type)
				current_type = overlap_list[idx]->type;
		}

		/* Continue building up new map based on this information: */
		if (current_type != last_type || !e820_type_mergeable(current_type)) {
			if (last_type) {
				new_entries[new_nr_entries].size = change_point[chg_idx]->addr - last_addr;
				/* Move forward only if the new size was non-zero: */
				if (new_entries[new_nr_entries].size != 0)
					/* No more space left for new entries? */
					if (++new_nr_entries >= max_nr_entries)
						break;
			}
			if (current_type) {
				new_entries[new_nr_entries].addr = change_point[chg_idx]->addr;
				new_entries[new_nr_entries].type = current_type;
				last_addr = change_point[chg_idx]->addr;
			}
			last_type = current_type;
		}
	}

	/* Copy the new entries into the original location: */
	memcpy(entries, new_entries, new_nr_entries*sizeof(*entries));
	table->nr_entries = new_nr_entries;

	return 0;
}

/*
 * Copy the BIOS E820 map into the kernel's e820_table.
 *
 * Sanity-check it while we're at it..
 */
__init static int append_e820_table(struct boot_e820_entry *entries, u32 nr_entries)
{
	struct boot_e820_entry *entry = entries;

	while (nr_entries) {
		u64 start = entry->addr;
		u64 size  = entry->size;
		u64 end   = start + size-1;
		u32 type  = entry->type;

		/* Ignore the remaining entries on 64-bit overflow: */
		if (start > end && likely(size))
			return -1;

		e820__range_add(start, size, type);

		entry++;
		nr_entries--;
	}
	return 0;
}

__init static u64
__e820__range_update(struct e820_table *table, u64 start, u64 size, enum e820_type old_type, enum e820_type new_type)
{
	u64 end;
	u32 idx;
	u64 real_updated_size = 0;

	BUG_ON(old_type == new_type);

	if (size > (ULLONG_MAX - start))
		size = ULLONG_MAX - start;

	end = start + size;
	printk(KERN_DEBUG "e820: update [mem %#010Lx-%#010Lx]", start, end - 1);
	e820_print_type(old_type);
	pr_cont(" ==>");
	e820_print_type(new_type);
	pr_cont("\n");

	for (idx = 0; idx < table->nr_entries; idx++) {
		struct e820_entry *entry = &table->entries[idx];
		u64 final_start, final_end;
		u64 entry_end;

		if (entry->type != old_type)
			continue;

		entry_end = entry->addr + entry->size;

		/* Completely covered by new range? */
		if (entry->addr >= start && entry_end <= end) {
			entry->type = new_type;
			real_updated_size += entry->size;
			continue;
		}

		/* New range is completely covered? */
		if (entry->addr < start && entry_end > end) {
			__e820__range_add(table, start, size, new_type);
			__e820__range_add(table, end, entry_end - end, entry->type);
			entry->size = start - entry->addr;
			real_updated_size += size;
			continue;
		}

		/* Partially covered: */
		final_start = max(start, entry->addr);
		final_end = min(end, entry_end);
		if (final_start >= final_end)
			continue;

		__e820__range_add(table, final_start, final_end - final_start, new_type);

		real_updated_size += final_end - final_start;

		/*
		 * Left range could be head or tail, so need to update
		 * its size first:
		 */
		entry->size -= final_end - final_start;
		if (entry->addr < final_start)
			continue;

		entry->addr = final_end;
	}
	return real_updated_size;
}

__init u64 e820__range_update(u64 start, u64 size, enum e820_type old_type, enum e820_type new_type)
{
	return __e820__range_update(e820_table, start, size, old_type, new_type);
}

__init u64 e820__range_update_table(struct e820_table *t, u64 start, u64 size,
				    enum e820_type old_type, enum e820_type new_type)
{
	return __e820__range_update(t, start, size, old_type, new_type);
}

/* Remove a range of memory from the E820 table: */
__init void e820__range_remove(u64 start, u64 size, enum e820_type filter_type)
{
	u32 idx;
	u64 end;

	if (size > (ULLONG_MAX - start))
		size = ULLONG_MAX - start;

	end = start + size;
	printk(KERN_DEBUG "e820: remove [mem %#010Lx-%#010Lx]", start, end - 1);
	if (filter_type)
		e820_print_type(filter_type);
	pr_cont("\n");

	for (idx = 0; idx < e820_table->nr_entries; idx++) {
		struct e820_entry *entry = &e820_table->entries[idx];
		u64 final_start, final_end;
		u64 entry_end;

		if (filter_type && entry->type != filter_type)
			continue;

		entry_end = entry->addr + entry->size;

		/* Completely covered? */
		if (entry->addr >= start && entry_end <= end) {
			memset(entry, 0, sizeof(*entry));
			continue;
		}

		/* Is the new range completely covered? */
		if (entry->addr < start && entry_end > end) {
			e820__range_add(end, entry_end - end, entry->type);
			entry->size = start - entry->addr;
			continue;
		}

		/* Partially covered: */
		final_start = max(start, entry->addr);
		final_end = min(end, entry_end);
		if (final_start >= final_end)
			continue;

		/*
		 * Left range could be head or tail, so need to update
		 * the size first:
		 */
		entry->size -= final_end - final_start;
		if (entry->addr < final_start)
			continue;

		entry->addr = final_end;
	}
}

__init void e820__update_table_print(void)
{
	if (e820__update_table(e820_table))
		return;

	pr_info("modified physical RAM map:\n");
	e820__print_table("modified");
}

__init static void e820__update_table_kexec(void)
{
	e820__update_table(e820_table_kexec);
}

#define MAX_GAP_END SZ_4G

/*
 * Search for a gap in the E820 memory space from 0 to MAX_GAP_END (4GB).
 */
__init static int e820_search_gap(unsigned long *max_gap_start, unsigned long *max_gap_size)
{
	struct e820_entry *entry;
	u64 range_end_prev = 0;
	int found = 0;
	u32 idx;

	for (idx = 0; idx < e820_table->nr_entries; idx++) {
		u64 range_start, range_end;

		entry = e820_table->entries + idx;
		range_start = entry->addr;
		range_end   = entry->addr + entry->size;

		/* Process any gap before this entry: */
		if (range_start > range_end_prev) {
			u64 gap_start = range_end_prev;
			u64 gap_end = range_start;
			u64 gap_size;

			if (gap_start < MAX_GAP_END) {
				/* Make sure the entirety of the gap is below MAX_GAP_END: */
				gap_end = min(gap_end, MAX_GAP_END);
				gap_size = gap_end-gap_start;

				if (gap_size >= *max_gap_size) {
					*max_gap_start = gap_start;
					*max_gap_size = gap_size;
					found = 1;
				}
			}
		}

		range_end_prev = range_end;
	}

	/* Is there a usable gap beyond the last entry: */
	if (entry->addr + entry->size < MAX_GAP_END) {
		u64 gap_start = entry->addr + entry->size;
		u64 gap_size = MAX_GAP_END-gap_start;

		if (gap_size >= *max_gap_size) {
			*max_gap_start = gap_start;
			*max_gap_size = gap_size;
			found = 1;
		}
	}

	return found;
}

/*
 * Search for the biggest gap in the low 32 bits of the E820
 * memory space. We pass this space to the PCI subsystem, so
 * that it can assign MMIO resources for hotplug or
 * unconfigured devices in.
 *
 * Hopefully the BIOS let enough space left.
 */
__init void e820__setup_pci_gap(void)
{
	unsigned long max_gap_start, max_gap_size;
	int found;

	/* The minimum eligible gap size is 4MB: */
	max_gap_size = SZ_4M;
	found  = e820_search_gap(&max_gap_start, &max_gap_size);

	if (!found) {
#ifdef CONFIG_X86_64
		max_gap_start = (max_pfn << PAGE_SHIFT) + SZ_1M;
		pr_err("Cannot find an available gap in the 32-bit address range\n");
		pr_err("PCI devices with unassigned 32-bit BARs may not work!\n");
#else
		max_gap_start = SZ_256M;
#endif
	}

	/*
	 * e820__reserve_resources_late() protects stolen RAM already:
	 */
	pci_mem_start = max_gap_start;

	pr_info("[gap %#010lx-%#010lx] available for PCI devices\n",
		max_gap_start, max_gap_start + max_gap_size-1);
}

/*
 * Called late during init, in free_initmem().
 *
 * Initial e820_table and e820_table_kexec are largish __initdata arrays.
 *
 * Copy them to a (usually much smaller) dynamically allocated area that is
 * sized precisely after the number of e820 entries.
 *
 * This is done after we've performed all the fixes and tweaks to the tables.
 * All functions which modify them are __init functions, which won't exist
 * after free_initmem().
 */
__init void e820__reallocate_tables(void)
{
	struct e820_table *n;
	int size;

	size = offsetof(struct e820_table, entries) + sizeof(struct e820_entry)*e820_table->nr_entries;
	n = kmemdup(e820_table, size, GFP_KERNEL);
	BUG_ON(!n);
	e820_table = n;

	size = offsetof(struct e820_table, entries) + sizeof(struct e820_entry)*e820_table_kexec->nr_entries;
	n = kmemdup(e820_table_kexec, size, GFP_KERNEL);
	BUG_ON(!n);
	e820_table_kexec = n;

	size = offsetof(struct e820_table, entries) + sizeof(struct e820_entry)*e820_table_firmware->nr_entries;
	n = kmemdup(e820_table_firmware, size, GFP_KERNEL);
	BUG_ON(!n);
	e820_table_firmware = n;
}

/*
 * Because of the small fixed size of struct boot_params, only the first
 * 128 E820 memory entries are passed to the kernel via boot_params.e820_table,
 * the remaining (if any) entries are passed via the SETUP_E820_EXT node of
 * struct setup_data, which is parsed here.
 */
__init void e820__memory_setup_extended(u64 phys_addr, u32 data_len)
{
	int entries;
	struct boot_e820_entry *extmap;
	struct setup_data *sdata;

	sdata = early_memremap(phys_addr, data_len);
	entries = sdata->len / sizeof(*extmap);
	extmap = (struct boot_e820_entry *)(sdata->data);

	append_e820_table(extmap, entries);
	e820__update_table(e820_table);

	memcpy(e820_table_kexec, e820_table, sizeof(*e820_table_kexec));
	memcpy(e820_table_firmware, e820_table, sizeof(*e820_table_firmware));

	early_memunmap(sdata, data_len);
	pr_info("extended physical RAM map:\n");
	e820__print_table("extended");
}

/*
 * Find the ranges of physical addresses that do not correspond to
 * E820 RAM areas and register the corresponding pages as 'nosave' for
 * hibernation (32-bit) or software suspend and suspend to RAM (64-bit).
 *
 * This function requires the E820 map to be sorted and without any
 * overlapping entries.
 */
__init void e820__register_nosave_regions(unsigned long limit_pfn)
{
	u32 idx;
	u64 last_addr = 0;

	for (idx = 0; idx < e820_table->nr_entries; idx++) {
		struct e820_entry *entry = &e820_table->entries[idx];

		if (entry->type != E820_TYPE_RAM)
			continue;

		if (last_addr < entry->addr)
			register_nosave_region(PFN_DOWN(last_addr), PFN_UP(entry->addr));

		last_addr = entry->addr + entry->size;
	}

	register_nosave_region(PFN_DOWN(last_addr), limit_pfn);
}

#ifdef CONFIG_ACPI
/*
 * Register ACPI NVS memory regions, so that we can save/restore them during
 * hibernation and the subsequent resume:
 */
__init static int e820__register_nvs_regions(void)
{
	u32 idx;

	for (idx = 0; idx < e820_table->nr_entries; idx++) {
		struct e820_entry *entry = &e820_table->entries[idx];

		if (entry->type == E820_TYPE_NVS)
			acpi_nvs_register(entry->addr, entry->size);
	}

	return 0;
}
core_initcall(e820__register_nvs_regions);
#endif

/*
 * Allocate the requested number of bytes with the requested alignment
 * and return (the physical address) to the caller. Also register this
 * range in the 'kexec' E820 table as a reserved range.
 *
 * This allows kexec to fake a new mptable, as if it came from the real
 * system.
 */
__init u64 e820__memblock_alloc_reserved(u64 size, u64 align)
{
	u64 addr;

	addr = memblock_phys_alloc(size, align);
	if (addr) {
		e820__range_update_table(e820_table_kexec, addr, size, E820_TYPE_RAM, E820_TYPE_RESERVED);
		pr_info("update e820_table_kexec for e820__memblock_alloc_reserved()\n");
		e820__update_table_kexec();
	}

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
__init static unsigned long e820__end_ram_pfn(unsigned long limit_pfn)
{
	u32 idx;
	unsigned long last_pfn = 0;
	unsigned long max_arch_pfn = MAX_ARCH_PFN;

	for (idx = 0; idx < e820_table->nr_entries; idx++) {
		struct e820_entry *entry = &e820_table->entries[idx];
		unsigned long start_pfn;
		unsigned long end_pfn;

		if (entry->type != E820_TYPE_RAM &&
		    entry->type != E820_TYPE_ACPI)
			continue;

		start_pfn = entry->addr >> PAGE_SHIFT;
		end_pfn = (entry->addr + entry->size) >> PAGE_SHIFT;

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

	pr_info("last_pfn = %#lx max_arch_pfn = %#lx\n",
		last_pfn, max_arch_pfn);
	return last_pfn;
}

__init unsigned long e820__end_of_ram_pfn(void)
{
	return e820__end_ram_pfn(MAX_ARCH_PFN);
}

__init unsigned long e820__end_of_low_ram_pfn(void)
{
	return e820__end_ram_pfn(1UL << (32 - PAGE_SHIFT));
}

__initdata static int userdef;

/* The "mem=nopentium" boot option disables 4MB page tables on 32-bit kernels: */
__init static int parse_memopt(char *p)
{
	u64 mem_size;

	if (!p)
		return -EINVAL;

	if (!strcmp(p, "nopentium")) {
#ifdef CONFIG_X86_32
		setup_clear_cpu_cap(X86_FEATURE_PSE);
		return 0;
#else
		pr_warn("mem=nopentium ignored! (only supported on x86_32)\n");
		return -EINVAL;
#endif
	}

	userdef = 1;
	mem_size = memparse(p, &p);

	/* Don't remove all memory when getting "mem={invalid}" parameter: */
	if (mem_size == 0)
		return -EINVAL;

	e820__range_remove(mem_size, ULLONG_MAX - mem_size, E820_TYPE_RAM);

#ifdef CONFIG_MEMORY_HOTPLUG
	max_mem_size = mem_size;
#endif

	return 0;
}
early_param("mem", parse_memopt);

__init static int parse_memmap_one(char *p)
{
	char *oldp;
	u64 start_at, mem_size;

	if (!p)
		return -EINVAL;

	if (!strncmp(p, "exactmap", 8)) {
		e820_table->nr_entries = 0;
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
		e820__range_add(start_at, mem_size, E820_TYPE_RAM);
	} else if (*p == '#') {
		start_at = memparse(p+1, &p);
		e820__range_add(start_at, mem_size, E820_TYPE_ACPI);
	} else if (*p == '$') {
		start_at = memparse(p+1, &p);
		e820__range_add(start_at, mem_size, E820_TYPE_RESERVED);
	} else if (*p == '!') {
		start_at = memparse(p+1, &p);
		e820__range_add(start_at, mem_size, E820_TYPE_PRAM);
	} else if (*p == '%') {
		enum e820_type from = 0, to = 0;

		start_at = memparse(p + 1, &p);
		if (*p == '-')
			from = simple_strtoull(p + 1, &p, 0);
		if (*p == '+')
			to = simple_strtoull(p + 1, &p, 0);
		if (*p != '\0')
			return -EINVAL;
		if (from && to)
			e820__range_update(start_at, mem_size, from, to);
		else if (to)
			e820__range_add(start_at, mem_size, to);
		else
			e820__range_remove(start_at, mem_size, from);
	} else {
		e820__range_remove(mem_size, ULLONG_MAX - mem_size, E820_TYPE_RAM);
	}

	return *p == '\0' ? 0 : -EINVAL;
}

__init static int parse_memmap_opt(char *str)
{
	while (str) {
		char *k = strchr(str, ',');

		if (k)
			*k++ = 0;

		parse_memmap_one(str);
		str = k;
	}

	return 0;
}
early_param("memmap", parse_memmap_opt);

/*
 * Called after parse_early_param(), after early parameters (such as mem=)
 * have been processed, in which case we already have an E820 table filled in
 * via the parameter callback function(s), but it's not sorted and printed yet:
 */
__init void e820__finish_early_params(void)
{
	if (userdef) {
		if (e820__update_table(e820_table) < 0)
			panic("Invalid user supplied memory map");

		pr_info("user-defined physical RAM map:\n");
		e820__print_table("user");
	}
}

__init static const char * e820_type_to_string(struct e820_entry *entry)
{
	switch (entry->type) {
	case E820_TYPE_RAM:		return "System RAM";
	case E820_TYPE_ACPI:		return "ACPI Tables";
	case E820_TYPE_NVS:		return "ACPI Non-volatile Storage";
	case E820_TYPE_UNUSABLE:	return "Unusable memory";
	case E820_TYPE_PRAM:		return "Persistent Memory (legacy)";
	case E820_TYPE_PMEM:		return "Persistent Memory";
	case E820_TYPE_RESERVED:	return "Reserved";
	case E820_TYPE_SOFT_RESERVED:	return "Soft Reserved";
	default:			return "Unknown E820 type";
	}
}

__init static unsigned long e820_type_to_iomem_type(struct e820_entry *entry)
{
	switch (entry->type) {
	case E820_TYPE_RAM:		return IORESOURCE_SYSTEM_RAM;
	case E820_TYPE_ACPI:		/* Fall-through: */
	case E820_TYPE_NVS:		/* Fall-through: */
	case E820_TYPE_UNUSABLE:	/* Fall-through: */
	case E820_TYPE_PRAM:		/* Fall-through: */
	case E820_TYPE_PMEM:		/* Fall-through: */
	case E820_TYPE_RESERVED:	/* Fall-through: */
	case E820_TYPE_SOFT_RESERVED:	/* Fall-through: */
	default:			return IORESOURCE_MEM;
	}
}

__init static unsigned long e820_type_to_iores_desc(struct e820_entry *entry)
{
	switch (entry->type) {
	case E820_TYPE_ACPI:		return IORES_DESC_ACPI_TABLES;
	case E820_TYPE_NVS:		return IORES_DESC_ACPI_NV_STORAGE;
	case E820_TYPE_PMEM:		return IORES_DESC_PERSISTENT_MEMORY;
	case E820_TYPE_PRAM:		return IORES_DESC_PERSISTENT_MEMORY_LEGACY;
	case E820_TYPE_RESERVED:	return IORES_DESC_RESERVED;
	case E820_TYPE_SOFT_RESERVED:	return IORES_DESC_SOFT_RESERVED;
	case E820_TYPE_RAM:		/* Fall-through: */
	case E820_TYPE_UNUSABLE:	/* Fall-through: */
	default:			return IORES_DESC_NONE;
	}
}

/*
 * We assign one resource entry for each E820 map entry:
 */
__initdata static struct resource *e820_res;

/*
 * Is this a device address region that should not be marked busy?
 * (Versus system address regions that we register & lock early.)
 */
__init static bool e820_device_region(enum e820_type type, struct resource *res)
{
	/* This is the legacy BIOS/DOS ROM-shadow + MMIO region: */
	if (res->start < SZ_1M)
		return false;

	/*
	 * Treat persistent memory and other special memory ranges like
	 * device memory, i.e. keep it available for exclusive use of a
	 * driver:
	 */
	switch (type) {
	case E820_TYPE_RESERVED:
	case E820_TYPE_SOFT_RESERVED:
	case E820_TYPE_PRAM:
	case E820_TYPE_PMEM:
		return true;
	case E820_TYPE_RAM:
	case E820_TYPE_ACPI:
	case E820_TYPE_NVS:
	case E820_TYPE_UNUSABLE:
	default:
		return false;
	}
}

/*
 * Mark E820 system regions as busy for the resource manager:
 */
__init void e820__reserve_resources(void)
{
	u32 idx;
	struct resource *res;
	u64 end;

	res = memblock_alloc_or_panic(sizeof(*res) * e820_table->nr_entries,
			     SMP_CACHE_BYTES);
	e820_res = res;

	for (idx = 0; idx < e820_table->nr_entries; idx++) {
		struct e820_entry *entry = e820_table->entries + idx;

		end = entry->addr + entry->size - 1;
		if (end != (resource_size_t)end) {
			res++;
			continue;
		}
		res->start = entry->addr;
		res->end   = end;
		res->name  = e820_type_to_string(entry);
		res->flags = e820_type_to_iomem_type(entry);
		res->desc  = e820_type_to_iores_desc(entry);

		/*
		 * Skip and don't register device regions that could be conflicted
		 * with PCI device BAR resources. They get inserted later in
		 * pcibios_resource_survey() -> e820__reserve_resources_late():
		 */
		if (!e820_device_region(entry->type, res)) {
			res->flags |= IORESOURCE_BUSY;
			insert_resource(&iomem_resource, res);
		}
		res++;
	}

	/* Expose the kexec e820 table to sysfs: */
	for (idx = 0; idx < e820_table_kexec->nr_entries; idx++) {
		struct e820_entry *entry = e820_table_kexec->entries + idx;

		firmware_map_add_early(entry->addr, entry->addr + entry->size, e820_type_to_string(entry));
	}
}

/*
 * How much should we pad the end of RAM, depending on where it is?
 */
__init static unsigned long ram_alignment(resource_size_t pos)
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

__init void e820__reserve_resources_late(void)
{
	/*
	 * Register device address regions listed in the E820 map,
	 * these can be claimed by device drivers later on:
	 */
	for (u32 idx = 0; idx < e820_table->nr_entries; idx++) {
		struct resource *res = e820_res + idx;

		/* skip added or uninitialized resources */
		if (res->parent || !res->end)
			continue;

		/* set aside soft-reserved resources for driver consideration */
		if (res->desc == IORES_DESC_SOFT_RESERVED) {
			insert_resource_expand_to_fit(&soft_reserve_resource, res);
		} else {
			/* publish the rest immediately */
			insert_resource_expand_to_fit(&iomem_resource, res);
		}
	}

	/*
	 * Create additional 'gaps' at the end of RAM regions,
	 * rounding them up to 64k/1MB/64MB boundaries, should
	 * they be weirdly sized, and register extra, locked
	 * resource regions for them, to make sure drivers
	 * won't claim those addresses.
	 *
	 * These are basically blind guesses and heuristics to
	 * avoid resource conflicts with broken firmware that
	 * doesn't properly list 'stolen RAM' as a system region
	 * in the E820 map.
	 */
	for (u32 idx = 0; idx < e820_table->nr_entries; idx++) {
		struct e820_entry *entry = &e820_table->entries[idx];
		u64 start, end;

		if (entry->type != E820_TYPE_RAM)
			continue;

		start = entry->addr + entry->size;
		end = round_up(start, ram_alignment(start)) - 1;
		if (end > MAX_RESOURCE_SIZE)
			end = MAX_RESOURCE_SIZE;
		if (start >= end)
			continue;

		pr_info("e820: register RAM buffer resource [mem %#010llx-%#010llx]\n", start, end);
		reserve_region_with_split(&iomem_resource, start, end, "RAM buffer");
	}
}

/*
 * Pass the firmware (bootloader) E820 map to the kernel and process it:
 */
__init char * e820__memory_setup_default(void)
{
	char *who = "BIOS-e820";

	/*
	 * Try to copy the BIOS-supplied E820-map.
	 *
	 * Otherwise fake a memory map; one section from 0k->640k,
	 * the next section from 1mb->appropriate_mem_k
	 */
	if (append_e820_table(boot_params.e820_table, boot_params.e820_entries) < 0) {
		u64 mem_size;

		/* Compare results from other methods and take the one that gives more RAM: */
		if (boot_params.alt_mem_k < boot_params.screen_info.ext_mem_k) {
			mem_size = boot_params.screen_info.ext_mem_k;
			who = "BIOS-88";
		} else {
			mem_size = boot_params.alt_mem_k;
			who = "BIOS-e801";
		}

		e820_table->nr_entries = 0;
		e820__range_add(0, LOWMEMSIZE(), E820_TYPE_RAM);
		e820__range_add(HIGH_MEMORY, mem_size << 10, E820_TYPE_RAM);
	}

	/* We just appended a lot of ranges, sanitize the table: */
	e820__update_table(e820_table);

	return who;
}

/*
 * Calls e820__memory_setup_default() in essence to pick up the firmware/bootloader
 * E820 map - with an optional platform quirk available for virtual platforms
 * to override this method of boot environment processing:
 */
__init void e820__memory_setup(void)
{
	char *who;

	/* This is a firmware interface ABI - make sure we don't break it: */
	BUILD_BUG_ON(sizeof(struct boot_e820_entry) != 20);

	who = x86_init.resources.memory_setup();

	memcpy(e820_table_kexec, e820_table, sizeof(*e820_table_kexec));
	memcpy(e820_table_firmware, e820_table, sizeof(*e820_table_firmware));

	pr_info("BIOS-provided physical RAM map:\n");
	e820__print_table(who);
}

__init void e820__memblock_setup(void)
{
	u32 idx;
	u64 end;

#ifdef CONFIG_MEMORY_HOTPLUG
	/*
	 * Memory used by the kernel cannot be hot-removed because Linux
	 * cannot migrate the kernel pages. When memory hotplug is
	 * enabled, we should prevent memblock from allocating memory
	 * for the kernel.
	 *
	 * ACPI SRAT records all hotpluggable memory ranges. But before
	 * SRAT is parsed, we don't know about it.
	 *
	 * The kernel image is loaded into memory at very early time. We
	 * cannot prevent this anyway. So on NUMA system, we set any
	 * node the kernel resides in as un-hotpluggable.
	 *
	 * Since on modern servers, one node could have double-digit
	 * gigabytes memory, we can assume the memory around the kernel
	 * image is also un-hotpluggable. So before SRAT is parsed, just
	 * allocate memory near the kernel image to try the best to keep
	 * the kernel away from hotpluggable memory.
	 */
	if (movable_node_is_enabled())
		memblock_set_bottom_up(true);
#endif

	/*
	 * At this point only the first megabyte is mapped for sure, the
	 * rest of the memory cannot be used for memblock resizing
	 */
	memblock_set_current_limit(ISA_END_ADDRESS);

	/*
	 * The bootstrap memblock region count maximum is 128 entries
	 * (INIT_MEMBLOCK_REGIONS), but EFI might pass us more E820 entries
	 * than that - so allow memblock resizing.
	 *
	 * This is safe, because this call happens pretty late during x86 setup,
	 * so we know about reserved memory regions already. (This is important
	 * so that memblock resizing does no stomp over reserved areas.)
	 */
	memblock_allow_resize();

	for (idx = 0; idx < e820_table->nr_entries; idx++) {
		struct e820_entry *entry = &e820_table->entries[idx];

		end = entry->addr + entry->size;
		if (end != (resource_size_t)end)
			continue;

		if (entry->type == E820_TYPE_SOFT_RESERVED)
			memblock_reserve(entry->addr, entry->size);

		if (entry->type != E820_TYPE_RAM)
			continue;

		memblock_add(entry->addr, entry->size);
	}

	/*
	 * At this point memblock is only allowed to allocate from memory
	 * below 1M (aka ISA_END_ADDRESS) up until direct map is completely set
	 * up in init_mem_mapping().
	 *
	 * KHO kernels are special and use only scratch memory for memblock
	 * allocations, but memory below 1M is ignored by kernel after early
	 * boot and cannot be naturally marked as scratch.
	 *
	 * To allow allocation of the real-mode trampoline and a few (if any)
	 * other very early allocations from below 1M forcibly mark the memory
	 * below 1M as scratch.
	 *
	 * After real mode trampoline is allocated, we clear that scratch
	 * marking.
	 */
	memblock_mark_kho_scratch(0, SZ_1M);

	/*
	 * 32-bit systems are limited to 4BG of memory even with HIGHMEM and
	 * to even less without it.
	 * Discard memory after max_pfn - the actual limit detected at runtime.
	 */
	if (IS_ENABLED(CONFIG_X86_32))
		memblock_remove(PFN_PHYS(max_pfn), -1);

	/* Throw away partial pages: */
	memblock_trim_memory(PAGE_SIZE);

	memblock_dump_all();
}
