// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 Linaro Ltd;  <ard.biesheuvel@linaro.org>
 */

#include <linux/efi.h>
#include <linux/log2.h>
#include <asm/efi.h>

#include "efistub.h"

/*
 * Return the number of slots covered by this entry, i.e., the number of
 * addresses it covers that are suitably aligned and supply enough room
 * for the allocation.
 */
static unsigned long get_entry_num_slots(efi_memory_desc_t *md,
					 unsigned long size,
					 unsigned long align_shift,
					 u64 alloc_min, u64 alloc_max)
{
	unsigned long align = 1UL << align_shift;
	u64 first_slot, last_slot, region_end;

	if (md->type != EFI_CONVENTIONAL_MEMORY)
		return 0;

	if (efi_soft_reserve_enabled() &&
	    (md->attribute & EFI_MEMORY_SP))
		return 0;

	region_end = min(md->phys_addr + md->num_pages * EFI_PAGE_SIZE - 1,
			 alloc_max);
	if (region_end < size)
		return 0;

	first_slot = round_up(max(md->phys_addr, alloc_min), align);
	last_slot = round_down(region_end - size + 1, align);

	if (first_slot > last_slot)
		return 0;

	return ((unsigned long)(last_slot - first_slot) >> align_shift) + 1;
}

/*
 * The UEFI memory descriptors have a virtual address field that is only used
 * when installing the virtual mapping using SetVirtualAddressMap(). Since it
 * is unused here, we can reuse it to keep track of each descriptor's slot
 * count.
 */
#define MD_NUM_SLOTS(md)	((md)->virt_addr)

efi_status_t efi_random_alloc(unsigned long size,
			      unsigned long align,
			      unsigned long *addr,
			      unsigned long random_seed,
			      int memory_type,
			      unsigned long alloc_min,
			      unsigned long alloc_max)
{
	unsigned long total_slots = 0, target_slot;
	unsigned long total_mirrored_slots = 0;
	struct efi_boot_memmap *map;
	efi_status_t status;
	int map_offset;

	status = efi_get_memory_map(&map, false);
	if (status != EFI_SUCCESS)
		return status;

	if (align < EFI_ALLOC_ALIGN)
		align = EFI_ALLOC_ALIGN;

	size = round_up(size, EFI_ALLOC_ALIGN);

	/* count the suitable slots in each memory map entry */
	for (map_offset = 0; map_offset < map->map_size; map_offset += map->desc_size) {
		efi_memory_desc_t *md = (void *)map->map + map_offset;
		unsigned long slots;

		slots = get_entry_num_slots(md, size, ilog2(align), alloc_min,
					    alloc_max);
		MD_NUM_SLOTS(md) = slots;
		total_slots += slots;
		if (md->attribute & EFI_MEMORY_MORE_RELIABLE)
			total_mirrored_slots += slots;
	}

	/* consider only mirrored slots for randomization if any exist */
	if (total_mirrored_slots > 0)
		total_slots = total_mirrored_slots;

	/* find a random number between 0 and total_slots */
	target_slot = (total_slots * (u64)(random_seed & U32_MAX)) >> 32;

	/*
	 * target_slot is now a value in the range [0, total_slots), and so
	 * it corresponds with exactly one of the suitable slots we recorded
	 * when iterating over the memory map the first time around.
	 *
	 * So iterate over the memory map again, subtracting the number of
	 * slots of each entry at each iteration, until we have found the entry
	 * that covers our chosen slot. Use the residual value of target_slot
	 * to calculate the randomly chosen address, and allocate it directly
	 * using EFI_ALLOCATE_ADDRESS.
	 */
	for (map_offset = 0; map_offset < map->map_size; map_offset += map->desc_size) {
		efi_memory_desc_t *md = (void *)map->map + map_offset;
		efi_physical_addr_t target;
		unsigned long pages;

		if (total_mirrored_slots > 0 &&
		    !(md->attribute & EFI_MEMORY_MORE_RELIABLE))
			continue;

		if (target_slot >= MD_NUM_SLOTS(md)) {
			target_slot -= MD_NUM_SLOTS(md);
			continue;
		}

		target = round_up(md->phys_addr, align) + target_slot * align;
		pages = size / EFI_PAGE_SIZE;

		status = efi_bs_call(allocate_pages, EFI_ALLOCATE_ADDRESS,
				     memory_type, pages, &target);
		if (status == EFI_SUCCESS)
			*addr = target;
		break;
	}

	efi_bs_call(free_pool, map);

	return status;
}
