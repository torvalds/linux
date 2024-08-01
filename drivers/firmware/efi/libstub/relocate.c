// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

/**
 * efi_low_alloc_above() - allocate pages at or above given address
 * @size:	size of the memory area to allocate
 * @align:	minimum alignment of the allocated memory area. It should
 *		a power of two.
 * @addr:	on exit the address of the allocated memory
 * @min:	minimum address to used for the memory allocation
 *
 * Allocate at the lowest possible address that is not below @min as
 * EFI_LOADER_DATA. The allocated pages are aligned according to @align but at
 * least EFI_ALLOC_ALIGN. The first allocated page will not below the address
 * given by @min.
 *
 * Return:	status code
 */
efi_status_t efi_low_alloc_above(unsigned long size, unsigned long align,
				 unsigned long *addr, unsigned long min)
{
	struct efi_boot_memmap *map;
	efi_status_t status;
	unsigned long nr_pages;
	int i;

	status = efi_get_memory_map(&map, false);
	if (status != EFI_SUCCESS)
		goto fail;

	/*
	 * Enforce minimum alignment that EFI or Linux requires when
	 * requesting a specific address.  We are doing page-based (or
	 * larger) allocations, and both the address and size must meet
	 * alignment constraints.
	 */
	if (align < EFI_ALLOC_ALIGN)
		align = EFI_ALLOC_ALIGN;

	size = round_up(size, EFI_ALLOC_ALIGN);
	nr_pages = size / EFI_PAGE_SIZE;
	for (i = 0; i < map->map_size / map->desc_size; i++) {
		efi_memory_desc_t *desc;
		unsigned long m = (unsigned long)map->map;
		u64 start, end;

		desc = efi_memdesc_ptr(m, map->desc_size, i);

		if (desc->type != EFI_CONVENTIONAL_MEMORY)
			continue;

		if (efi_soft_reserve_enabled() &&
		    (desc->attribute & EFI_MEMORY_SP))
			continue;

		if (desc->num_pages < nr_pages)
			continue;

		start = desc->phys_addr;
		end = start + desc->num_pages * EFI_PAGE_SIZE;

		if (start < min)
			start = min;

		start = round_up(start, align);
		if ((start + size) > end)
			continue;

		status = efi_bs_call(allocate_pages, EFI_ALLOCATE_ADDRESS,
				     EFI_LOADER_DATA, nr_pages, &start);
		if (status == EFI_SUCCESS) {
			*addr = start;
			break;
		}
	}

	if (i == map->map_size / map->desc_size)
		status = EFI_NOT_FOUND;

	efi_bs_call(free_pool, map);
fail:
	return status;
}

/**
 * efi_relocate_kernel() - copy memory area
 * @image_addr:		pointer to address of memory area to copy
 * @image_size:		size of memory area to copy
 * @alloc_size:		minimum size of memory to allocate, must be greater or
 *			equal to image_size
 * @preferred_addr:	preferred target address
 * @alignment:		minimum alignment of the allocated memory area. It
 *			should be a power of two.
 * @min_addr:		minimum target address
 *
 * Copy a memory area to a newly allocated memory area aligned according
 * to @alignment but at least EFI_ALLOC_ALIGN. If the preferred address
 * is not available, the allocated address will not be below @min_addr.
 * On exit, @image_addr is updated to the target copy address that was used.
 *
 * This function is used to copy the Linux kernel verbatim. It does not apply
 * any relocation changes.
 *
 * Return:		status code
 */
efi_status_t efi_relocate_kernel(unsigned long *image_addr,
				 unsigned long image_size,
				 unsigned long alloc_size,
				 unsigned long preferred_addr,
				 unsigned long alignment,
				 unsigned long min_addr)
{
	unsigned long cur_image_addr;
	unsigned long new_addr = 0;
	efi_status_t status;
	unsigned long nr_pages;
	efi_physical_addr_t efi_addr = preferred_addr;

	if (!image_addr || !image_size || !alloc_size)
		return EFI_INVALID_PARAMETER;
	if (alloc_size < image_size)
		return EFI_INVALID_PARAMETER;

	cur_image_addr = *image_addr;

	/*
	 * The EFI firmware loader could have placed the kernel image
	 * anywhere in memory, but the kernel has restrictions on the
	 * max physical address it can run at.  Some architectures
	 * also have a preferred address, so first try to relocate
	 * to the preferred address.  If that fails, allocate as low
	 * as possible while respecting the required alignment.
	 */
	nr_pages = round_up(alloc_size, EFI_ALLOC_ALIGN) / EFI_PAGE_SIZE;
	status = efi_bs_call(allocate_pages, EFI_ALLOCATE_ADDRESS,
			     EFI_LOADER_DATA, nr_pages, &efi_addr);
	new_addr = efi_addr;
	/*
	 * If preferred address allocation failed allocate as low as
	 * possible.
	 */
	if (status != EFI_SUCCESS) {
		status = efi_low_alloc_above(alloc_size, alignment, &new_addr,
					     min_addr);
	}
	if (status != EFI_SUCCESS) {
		efi_err("Failed to allocate usable memory for kernel.\n");
		return status;
	}

	/*
	 * We know source/dest won't overlap since both memory ranges
	 * have been allocated by UEFI, so we can safely use memcpy.
	 */
	memcpy((void *)new_addr, (void *)cur_image_addr, image_size);

	/* Return the new address of the relocated image. */
	*image_addr = new_addr;

	return status;
}
