// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

/**
 * efi_get_memory_map() - get memory map
 * @map:		pointer to memory map pointer to which to assign the
 *			newly allocated memory map
 *
 * Retrieve the UEFI memory map. The allocated memory leaves room for
 * up to EFI_MMAP_NR_SLACK_SLOTS additional memory map entries.
 *
 * Return:	status code
 */
efi_status_t efi_get_memory_map(struct efi_boot_memmap **map)
{
	struct efi_boot_memmap *m, tmp;
	efi_status_t status;
	unsigned long size;

	tmp.map_size = 0;
	status = efi_bs_call(get_memory_map, &tmp.map_size, NULL, &tmp.map_key,
			     &tmp.desc_size, &tmp.desc_ver);
	if (status != EFI_BUFFER_TOO_SMALL)
		return EFI_LOAD_ERROR;

	size = tmp.map_size + tmp.desc_size * EFI_MMAP_NR_SLACK_SLOTS;
	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, sizeof(*m) + size,
			     (void **)&m);
	if (status != EFI_SUCCESS)
		return status;

	m->buff_size = m->map_size = size;
	status = efi_bs_call(get_memory_map, &m->map_size, m->map, &m->map_key,
			     &m->desc_size, &m->desc_ver);
	if (status != EFI_SUCCESS)
		goto free_map;

	*map = m;
	return EFI_SUCCESS;

free_map:
	efi_bs_call(free_pool, m);
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
