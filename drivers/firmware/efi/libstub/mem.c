// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

#define EFI_MMAP_NR_SLACK_SLOTS	8

static inline bool mmap_has_headroom(unsigned long buff_size,
				     unsigned long map_size,
				     unsigned long desc_size)
{
	unsigned long slack = buff_size - map_size;

	return slack / desc_size >= EFI_MMAP_NR_SLACK_SLOTS;
}

/**
 * efi_get_memory_map() - get memory map
 * @map:	on return pointer to memory map
 *
 * Retrieve the UEFI memory map. The allocated memory leaves room for
 * up to EFI_MMAP_NR_SLACK_SLOTS additional memory map entries.
 *
 * Return:	status code
 */
efi_status_t efi_get_memory_map(struct efi_boot_memmap *map)
{
	efi_memory_desc_t *m = NULL;
	efi_status_t status;
	unsigned long key;
	u32 desc_version;

	*map->desc_size =	sizeof(*m);
	*map->map_size =	*map->desc_size * 32;
	*map->buff_size =	*map->map_size;
again:
	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA,
			     *map->map_size, (void **)&m);
	if (status != EFI_SUCCESS)
		goto fail;

	*map->desc_size = 0;
	key = 0;
	status = efi_bs_call(get_memory_map, map->map_size, m,
			     &key, map->desc_size, &desc_version);
	if (status == EFI_BUFFER_TOO_SMALL ||
	    !mmap_has_headroom(*map->buff_size, *map->map_size,
			       *map->desc_size)) {
		efi_bs_call(free_pool, m);
		/*
		 * Make sure there is some entries of headroom so that the
		 * buffer can be reused for a new map after allocations are
		 * no longer permitted.  Its unlikely that the map will grow to
		 * exceed this headroom once we are ready to trigger
		 * ExitBootServices()
		 */
		*map->map_size += *map->desc_size * EFI_MMAP_NR_SLACK_SLOTS;
		*map->buff_size = *map->map_size;
		goto again;
	}

	if (status == EFI_SUCCESS) {
		if (map->key_ptr)
			*map->key_ptr = key;
		if (map->desc_ver)
			*map->desc_ver = desc_version;
	} else {
		efi_bs_call(free_pool, m);
	}

fail:
	*map->map = m;
	return status;
}

/**
 * efi_allocate_pages() - Allocate memory pages
 * @size:	minimum number of bytes to allocate
 * @addr:	On return the address of the first allocated page. The first
 *		allocated page has alignment EFI_ALLOC_ALIGN which is an
 *		architecture dependent multiple of the page size.
 * @max:	the address that the last allocated memory page shall not
 *		exceed
 *
 * Allocate pages as EFI_LOADER_DATA. The allocated pages are aligned according
 * to EFI_ALLOC_ALIGN. The last allocated page will not exceed the address
 * given by @max.
 *
 * Return:	status code
 */
efi_status_t efi_allocate_pages(unsigned long size, unsigned long *addr,
				unsigned long max)
{
	efi_physical_addr_t alloc_addr = ALIGN_DOWN(max + 1, EFI_ALLOC_ALIGN) - 1;
	int slack = EFI_ALLOC_ALIGN / EFI_PAGE_SIZE - 1;
	efi_status_t status;

	size = round_up(size, EFI_ALLOC_ALIGN);
	status = efi_bs_call(allocate_pages, EFI_ALLOCATE_MAX_ADDRESS,
			     EFI_LOADER_DATA, size / EFI_PAGE_SIZE + slack,
			     &alloc_addr);
	if (status != EFI_SUCCESS)
		return status;

	*addr = ALIGN((unsigned long)alloc_addr, EFI_ALLOC_ALIGN);

	if (slack > 0) {
		int l = (alloc_addr % EFI_ALLOC_ALIGN) / EFI_PAGE_SIZE;

		if (l) {
			efi_bs_call(free_pages, alloc_addr, slack - l + 1);
			slack = l - 1;
		}
		if (slack)
			efi_bs_call(free_pages, *addr + size, slack);
	}
	return EFI_SUCCESS;
}
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
	unsigned long map_size, desc_size, buff_size;
	efi_memory_desc_t *map;
	efi_status_t status;
	unsigned long nr_pages;
	int i;
	struct efi_boot_memmap boot_map;

	boot_map.map		= &map;
	boot_map.map_size	= &map_size;
	boot_map.desc_size	= &desc_size;
	boot_map.desc_ver	= NULL;
	boot_map.key_ptr	= NULL;
	boot_map.buff_size	= &buff_size;

	status = efi_get_memory_map(&boot_map);
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
	for (i = 0; i < map_size / desc_size; i++) {
		efi_memory_desc_t *desc;
		unsigned long m = (unsigned long)map;
		u64 start, end;

		desc = efi_early_memdesc_ptr(m, desc_size, i);

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

	if (i == map_size / desc_size)
		status = EFI_NOT_FOUND;

	efi_bs_call(free_pool, map);
fail:
	return status;
}

/**
 * efi_free() - free memory pages
 * @size:	size of the memory area to free in bytes
 * @addr:	start of the memory area to free (must be EFI_PAGE_SIZE
 *		aligned)
 *
 * @size is rounded up to a multiple of EFI_ALLOC_ALIGN which is an
 * architecture specific multiple of EFI_PAGE_SIZE. So this function should
 * only be used to return pages allocated with efi_allocate_pages() or
 * efi_low_alloc_above().
 */
void efi_free(unsigned long size, unsigned long addr)
{
	unsigned long nr_pages;

	if (!size)
		return;

	nr_pages = round_up(size, EFI_ALLOC_ALIGN) / EFI_PAGE_SIZE;
	efi_bs_call(free_pages, addr, nr_pages);
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
	 * also have a prefered address, so first try to relocate
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
		pr_efi_err("Failed to allocate usable memory for kernel.\n");
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
