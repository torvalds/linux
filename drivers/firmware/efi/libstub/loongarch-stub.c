// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Yun Liu <liuyun@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <asm/efi.h>
#include <asm/addrspace.h>
#include "efistub.h"

typedef void __noreturn (*kernel_entry_t)(bool efi, unsigned long fdt);

extern int kernel_asize;
extern int kernel_fsize;
extern int kernel_offset;
extern kernel_entry_t kernel_entry;

efi_status_t check_platform_features(void)
{
	return EFI_SUCCESS;
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

	kernel_addr = (unsigned long)&kernel_offset - kernel_offset;

	status = efi_relocate_kernel(&kernel_addr, kernel_fsize, kernel_asize,
				     PHYSADDR(VMLINUX_LOAD_ADDRESS), SZ_2M, 0x0);

	*image_addr = kernel_addr;
	*image_size = kernel_asize;

	return status;
}

void __noreturn efi_enter_kernel(unsigned long entrypoint, unsigned long fdt, unsigned long fdt_size)
{
	kernel_entry_t real_kernel_entry;

	/* Config Direct Mapping */
	csr_write64(CSR_DMW0_INIT, LOONGARCH_CSR_DMWIN0);
	csr_write64(CSR_DMW1_INIT, LOONGARCH_CSR_DMWIN1);

	real_kernel_entry = (kernel_entry_t)
		((unsigned long)&kernel_entry - entrypoint + VMLINUX_LOAD_ADDRESS);

	if (!efi_novamap)
		real_kernel_entry(true, fdt);
	else
		real_kernel_entry(false, fdt);
}
