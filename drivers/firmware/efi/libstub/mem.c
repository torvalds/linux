// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

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
	efi_physical_addr_t alloc_addr;
	efi_status_t status;

	if (EFI_ALLOC_ALIGN > EFI_PAGE_SIZE)
		return efi_allocate_pages_aligned(size, addr, max,
						  EFI_ALLOC_ALIGN);

	alloc_addr = ALIGN_DOWN(max + 1, EFI_ALLOC_ALIGN) - 1;
	status = efi_bs_call(allocate_pages, EFI_ALLOCATE_MAX_ADDRESS,
			     EFI_LOADER_DATA, DIV_ROUND_UP(size, EFI_PAGE_SIZE),
			     &alloc_addr);
	if (status != EFI_SUCCESS)
		return status;

	*addr = alloc_addr;
	return EFI_SUCCESS;
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
