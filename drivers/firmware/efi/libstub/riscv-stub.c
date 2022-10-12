// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/efi.h>

#include <asm/efi.h>
#include <asm/sections.h>
#include <asm/unaligned.h>

#include "efistub.h"

unsigned long stext_offset(void)
{
	/*
	 * When built as part of the kernel, the EFI stub cannot branch to the
	 * kernel proper via the image header, as the PE/COFF header is
	 * strictly not part of the in-memory presentation of the image, only
	 * of the file representation. So instead, we need to jump to the
	 * actual entrypoint in the .text region of the image.
	 */
	return _start_kernel - _start;
}

efi_status_t handle_kernel_image(unsigned long *image_addr,
				 unsigned long *image_size,
				 unsigned long *reserve_addr,
				 unsigned long *reserve_size,
				 efi_loaded_image_t *image,
				 efi_handle_t image_handle)
{
	unsigned long kernel_size = 0;
	unsigned long preferred_addr;
	efi_status_t status;

	kernel_size = _edata - _start;
	*image_addr = (unsigned long)_start;
	*image_size = kernel_size + (_end - _edata);

	/*
	 * RISC-V kernel maps PAGE_OFFSET virtual address to the same physical
	 * address where kernel is booted. That's why kernel should boot from
	 * as low as possible to avoid wastage of memory. Currently, dram_base
	 * is occupied by the firmware. So the preferred address for kernel to
	 * boot is next aligned address. If preferred address is not available,
	 * relocate_kernel will fall back to efi_low_alloc_above to allocate
	 * lowest possible memory region as long as the address and size meets
	 * the alignment constraints.
	 */
	preferred_addr = EFI_KIMG_PREFERRED_ADDRESS;
	status = efi_relocate_kernel(image_addr, kernel_size, *image_size,
				     preferred_addr, efi_get_kimg_min_align(),
				     0x0);

	if (status != EFI_SUCCESS) {
		efi_err("Failed to relocate kernel\n");
		*image_size = 0;
	}
	return status;
}
