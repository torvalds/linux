/*
 * Copyright (C) 2013, 2014 Linaro Ltd;  <roy.franz@linaro.org>
 *
 * This file implements the EFI boot stub for the arm64 kernel.
 * Adapted from ARM version by Mark Salter <msalter@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/efi.h>
#include <linux/libfdt.h>
#include <asm/sections.h>

/*
 * AArch64 requires the DTB to be 8-byte aligned in the first 512MiB from
 * start of kernel and may not cross a 2MiB boundary. We set alignment to
 * 2MiB so we know it won't cross a 2MiB boundary.
 */
#define EFI_FDT_ALIGN	SZ_2M   /* used by allocate_new_fdt_and_exit_boot() */
#define MAX_FDT_OFFSET	SZ_512M

#define efi_call_early(f, ...) sys_table_arg->boottime->f(__VA_ARGS__)

static void efi_char16_printk(efi_system_table_t *sys_table_arg,
			      efi_char16_t *str);

static efi_status_t efi_open_volume(efi_system_table_t *sys_table,
				    void *__image, void **__fh);
static efi_status_t efi_file_close(void *handle);

static efi_status_t
efi_file_read(void *handle, unsigned long *size, void *addr);

static efi_status_t
efi_file_size(efi_system_table_t *sys_table, void *__fh,
	      efi_char16_t *filename_16, void **handle, u64 *file_sz);

/* Include shared EFI stub code */
#include "../../../drivers/firmware/efi/efi-stub-helper.c"
#include "../../../drivers/firmware/efi/fdt.c"
#include "../../../drivers/firmware/efi/arm-stub.c"


static efi_status_t handle_kernel_image(efi_system_table_t *sys_table,
					unsigned long *image_addr,
					unsigned long *image_size,
					unsigned long *reserve_addr,
					unsigned long *reserve_size,
					unsigned long dram_base,
					efi_loaded_image_t *image)
{
	efi_status_t status;
	unsigned long kernel_size, kernel_memsize = 0;

	/* Relocate the image, if required. */
	kernel_size = _edata - _text;
	if (*image_addr != (dram_base + TEXT_OFFSET)) {
		kernel_memsize = kernel_size + (_end - _edata);
		status = efi_relocate_kernel(sys_table, image_addr,
					     kernel_size, kernel_memsize,
					     dram_base + TEXT_OFFSET,
					     PAGE_SIZE);
		if (status != EFI_SUCCESS) {
			pr_efi_err(sys_table, "Failed to relocate kernel\n");
			return status;
		}
		if (*image_addr != (dram_base + TEXT_OFFSET)) {
			pr_efi_err(sys_table, "Failed to alloc kernel memory\n");
			efi_free(sys_table, kernel_memsize, *image_addr);
			return EFI_ERROR;
		}
		*image_size = kernel_memsize;
	}


	return EFI_SUCCESS;
}
