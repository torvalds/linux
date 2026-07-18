// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Yun Liu <liuyun@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <asm/efi.h>
#include <asm/addrspace.h>
#include "efistub.h"
#include "loongarch-stub.h"

extern int kernel_asize;
extern int kernel_fsize;
extern int kernel_entry;

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
static
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
	efi_cache_sync_image(new_addr, image_size);

	/* Return the new address of the relocated image. */
	*image_addr = new_addr;

	return status;
}

efi_status_t handle_kernel_image(unsigned long *image_addr,
				 unsigned long *image_size,
				 unsigned long *reserve_addr,
				 unsigned long *reserve_size,
				 efi_loaded_image_t *image,
				 efi_handle_t image_handle)
{
	efi_status_t status;
	unsigned long kernel_addr = 0;

	kernel_addr = (unsigned long)image->image_base;

	status = efi_relocate_kernel(&kernel_addr, kernel_fsize, kernel_asize,
		     EFI_KIMG_PREFERRED_ADDRESS, efi_get_kimg_min_align(), 0x0);

	*image_addr = kernel_addr;
	*image_size = kernel_asize;

	return status;
}

unsigned long kernel_entry_address(unsigned long kernel_addr,
		efi_loaded_image_t *image)
{
	unsigned long base = (unsigned long)image->image_base;

	return (unsigned long)&kernel_entry - base + kernel_addr;
}
