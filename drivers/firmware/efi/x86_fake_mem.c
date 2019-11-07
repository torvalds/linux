// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights reserved. */
#include <linux/efi.h>
#include <asm/e820/api.h>
#include "fake_mem.h"

void __init efi_fake_memmap_early(void)
{
	int i;

	/*
	 * The late efi_fake_mem() call can handle all requests if
	 * EFI_MEMORY_SP support is disabled.
	 */
	if (!efi_soft_reserve_enabled())
		return;

	if (!efi_enabled(EFI_MEMMAP) || !nr_fake_mem)
		return;

	/*
	 * Given that efi_fake_memmap() needs to perform memblock
	 * allocations it needs to run after e820__memblock_setup().
	 * However, if efi_fake_mem specifies EFI_MEMORY_SP for a given
	 * address range that potentially needs to mark the memory as
	 * reserved prior to e820__memblock_setup(). Update e820
	 * directly if EFI_MEMORY_SP is specified for an
	 * EFI_CONVENTIONAL_MEMORY descriptor.
	 */
	for (i = 0; i < nr_fake_mem; i++) {
		struct efi_mem_range *mem = &efi_fake_mems[i];
		efi_memory_desc_t *md;
		u64 m_start, m_end;

		if ((mem->attribute & EFI_MEMORY_SP) == 0)
			continue;

		m_start = mem->range.start;
		m_end = mem->range.end;
		for_each_efi_memory_desc(md) {
			u64 start, end;

			if (md->type != EFI_CONVENTIONAL_MEMORY)
				continue;

			start = md->phys_addr;
			end = md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT) - 1;

			if (m_start <= end && m_end >= start)
				/* fake range overlaps descriptor */;
			else
				continue;

			/*
			 * Trim the boundary of the e820 update to the
			 * descriptor in case the fake range overlaps
			 * !EFI_CONVENTIONAL_MEMORY
			 */
			start = max(start, m_start);
			end = min(end, m_end);

			if (end <= start)
				continue;
			e820__range_update(start, end - start + 1, E820_TYPE_RAM,
					E820_TYPE_SOFT_RESERVED);
			e820__update_table(e820_table);
		}
	}
}
