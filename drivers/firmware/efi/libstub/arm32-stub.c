/*
 * Copyright (C) 2013 Linaro Ltd;  <roy.franz@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/efi.h>
#include <asm/efi.h>

efi_status_t check_platform_features(efi_system_table_t *sys_table_arg)
{
	int block;

	/* non-LPAE kernels can run anywhere */
	if (!IS_ENABLED(CONFIG_ARM_LPAE))
		return EFI_SUCCESS;

	/* LPAE kernels need compatible hardware */
	block = cpuid_feature_extract(CPUID_EXT_MMFR0, 0);
	if (block < 5) {
		pr_efi_err(sys_table_arg, "This LPAE kernel is not supported by your CPU\n");
		return EFI_UNSUPPORTED;
	}
	return EFI_SUCCESS;
}

static efi_guid_t screen_info_guid = LINUX_EFI_ARM_SCREEN_INFO_TABLE_GUID;

struct screen_info *alloc_screen_info(efi_system_table_t *sys_table_arg)
{
	struct screen_info *si;
	efi_status_t status;

	/*
	 * Unlike on arm64, where we can directly fill out the screen_info
	 * structure from the stub, we need to allocate a buffer to hold
	 * its contents while we hand over to the kernel proper from the
	 * decompressor.
	 */
	status = efi_call_early(allocate_pool, EFI_RUNTIME_SERVICES_DATA,
				sizeof(*si), (void **)&si);

	if (status != EFI_SUCCESS)
		return NULL;

	status = efi_call_early(install_configuration_table,
				&screen_info_guid, si);
	if (status == EFI_SUCCESS)
		return si;

	efi_call_early(free_pool, si);
	return NULL;
}

void free_screen_info(efi_system_table_t *sys_table_arg, struct screen_info *si)
{
	if (!si)
		return;

	efi_call_early(install_configuration_table, &screen_info_guid, NULL);
	efi_call_early(free_pool, si);
}

efi_status_t handle_kernel_image(efi_system_table_t *sys_table,
				 unsigned long *image_addr,
				 unsigned long *image_size,
				 unsigned long *reserve_addr,
				 unsigned long *reserve_size,
				 unsigned long dram_base,
				 efi_loaded_image_t *image)
{
	unsigned long nr_pages;
	efi_status_t status;
	/* Use alloc_addr to tranlsate between types */
	efi_physical_addr_t alloc_addr;

	/*
	 * Verify that the DRAM base address is compatible with the ARM
	 * boot protocol, which determines the base of DRAM by masking
	 * off the low 27 bits of the address at which the zImage is
	 * loaded. These assumptions are made by the decompressor,
	 * before any memory map is available.
	 */
	dram_base = round_up(dram_base, SZ_128M);

	/*
	 * Reserve memory for the uncompressed kernel image. This is
	 * all that prevents any future allocations from conflicting
	 * with the kernel. Since we can't tell from the compressed
	 * image how much DRAM the kernel actually uses (due to BSS
	 * size uncertainty) we allocate the maximum possible size.
	 * Do this very early, as prints can cause memory allocations
	 * that may conflict with this.
	 */
	alloc_addr = dram_base;
	*reserve_size = MAX_UNCOMP_KERNEL_SIZE;
	nr_pages = round_up(*reserve_size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
	status = sys_table->boottime->allocate_pages(EFI_ALLOCATE_ADDRESS,
						     EFI_LOADER_DATA,
						     nr_pages, &alloc_addr);
	if (status != EFI_SUCCESS) {
		*reserve_size = 0;
		pr_efi_err(sys_table, "Unable to allocate memory for uncompressed kernel.\n");
		return status;
	}
	*reserve_addr = alloc_addr;

	/*
	 * Relocate the zImage, so that it appears in the lowest 128 MB
	 * memory window.
	 */
	*image_size = image->image_size;
	status = efi_relocate_kernel(sys_table, image_addr, *image_size,
				     *image_size,
				     dram_base + MAX_UNCOMP_KERNEL_SIZE, 0);
	if (status != EFI_SUCCESS) {
		pr_efi_err(sys_table, "Failed to relocate kernel.\n");
		efi_free(sys_table, *reserve_size, *reserve_addr);
		*reserve_size = 0;
		return status;
	}

	/*
	 * Check to see if we were able to allocate memory low enough
	 * in memory. The kernel determines the base of DRAM from the
	 * address at which the zImage is loaded.
	 */
	if (*image_addr + *image_size > dram_base + ZIMAGE_OFFSET_LIMIT) {
		pr_efi_err(sys_table, "Failed to relocate kernel, no low memory available.\n");
		efi_free(sys_table, *reserve_size, *reserve_addr);
		*reserve_size = 0;
		efi_free(sys_table, *image_size, *image_addr);
		*image_size = 0;
		return EFI_LOAD_ERROR;
	}
	return EFI_SUCCESS;
}
