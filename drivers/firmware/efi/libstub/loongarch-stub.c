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
