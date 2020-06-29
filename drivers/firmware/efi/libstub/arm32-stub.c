// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 Linaro Ltd;  <roy.franz@linaro.org>
 */
#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

efi_status_t check_platform_features(void)
{
	int block;

	/* non-LPAE kernels can run anywhere */
	if (!IS_ENABLED(CONFIG_ARM_LPAE))
		return EFI_SUCCESS;

	/* LPAE kernels need compatible hardware */
	block = cpuid_feature_extract(CPUID_EXT_MMFR0, 0);
	if (block < 5) {
		efi_err("This LPAE kernel is not supported by your CPU\n");
		return EFI_UNSUPPORTED;
	}
	return EFI_SUCCESS;
}

static efi_guid_t screen_info_guid = LINUX_EFI_ARM_SCREEN_INFO_TABLE_GUID;

struct screen_info *alloc_screen_info(void)
{
	struct screen_info *si;
	efi_status_t status;

	/*
	 * Unlike on arm64, where we can directly fill out the screen_info
	 * structure from the stub, we need to allocate a buffer to hold
	 * its contents while we hand over to the kernel proper from the
	 * decompressor.
	 */
	status = efi_bs_call(allocate_pool, EFI_RUNTIME_SERVICES_DATA,
			     sizeof(*si), (void **)&si);

	if (status != EFI_SUCCESS)
		return NULL;

	status = efi_bs_call(install_configuration_table,
			     &screen_info_guid, si);
	if (status == EFI_SUCCESS)
		return si;

	efi_bs_call(free_pool, si);
	return NULL;
}

void free_screen_info(struct screen_info *si)
{
	if (!si)
		return;

	efi_bs_call(install_configuration_table, &screen_info_guid, NULL);
	efi_bs_call(free_pool, si);
}

static efi_status_t reserve_kernel_base(unsigned long dram_base,
					unsigned long *reserve_addr,
					unsigned long *reserve_size)
{
	efi_physical_addr_t alloc_addr;
	efi_memory_desc_t *memory_map;
	unsigned long nr_pages, map_size, desc_size, buff_size;
	efi_status_t status;
	unsigned long l;

	struct efi_boot_memmap map = {
		.map		= &memory_map,
		.map_size	= &map_size,
		.desc_size	= &desc_size,
		.desc_ver	= NULL,
		.key_ptr	= NULL,
		.buff_size	= &buff_size,
	};

	/*
	 * Reserve memory for the uncompressed kernel image. This is
	 * all that prevents any future allocations from conflicting
	 * with the kernel. Since we can't tell from the compressed
	 * image how much DRAM the kernel actually uses (due to BSS
	 * size uncertainty) we allocate the maximum possible size.
	 * Do this very early, as prints can cause memory allocations
	 * that may conflict with this.
	 */
	alloc_addr = dram_base + MAX_UNCOMP_KERNEL_SIZE;
	nr_pages = MAX_UNCOMP_KERNEL_SIZE / EFI_PAGE_SIZE;
	status = efi_bs_call(allocate_pages, EFI_ALLOCATE_MAX_ADDRESS,
			     EFI_BOOT_SERVICES_DATA, nr_pages, &alloc_addr);
	if (status == EFI_SUCCESS) {
		if (alloc_addr == dram_base) {
			*reserve_addr = alloc_addr;
			*reserve_size = MAX_UNCOMP_KERNEL_SIZE;
			return EFI_SUCCESS;
		}
		/*
		 * If we end up here, the allocation succeeded but starts below
		 * dram_base. This can only occur if the real base of DRAM is
		 * not a multiple of 128 MB, in which case dram_base will have
		 * been rounded up. Since this implies that a part of the region
		 * was already occupied, we need to fall through to the code
		 * below to ensure that the existing allocations don't conflict.
		 * For this reason, we use EFI_BOOT_SERVICES_DATA above and not
		 * EFI_LOADER_DATA, which we wouldn't able to distinguish from
		 * allocations that we want to disallow.
		 */
	}

	/*
	 * If the allocation above failed, we may still be able to proceed:
	 * if the only allocations in the region are of types that will be
	 * released to the OS after ExitBootServices(), the decompressor can
	 * safely overwrite them.
	 */
	status = efi_get_memory_map(&map);
	if (status != EFI_SUCCESS) {
		efi_err("reserve_kernel_base(): Unable to retrieve memory map.\n");
		return status;
	}

	for (l = 0; l < map_size; l += desc_size) {
		efi_memory_desc_t *desc;
		u64 start, end;

		desc = (void *)memory_map + l;
		start = desc->phys_addr;
		end = start + desc->num_pages * EFI_PAGE_SIZE;

		/* Skip if entry does not intersect with region */
		if (start >= dram_base + MAX_UNCOMP_KERNEL_SIZE ||
		    end <= dram_base)
			continue;

		switch (desc->type) {
		case EFI_BOOT_SERVICES_CODE:
		case EFI_BOOT_SERVICES_DATA:
			/* Ignore types that are released to the OS anyway */
			continue;

		case EFI_CONVENTIONAL_MEMORY:
			/* Skip soft reserved conventional memory */
			if (efi_soft_reserve_enabled() &&
			    (desc->attribute & EFI_MEMORY_SP))
				continue;

			/*
			 * Reserve the intersection between this entry and the
			 * region.
			 */
			start = max(start, (u64)dram_base);
			end = min(end, (u64)dram_base + MAX_UNCOMP_KERNEL_SIZE);

			status = efi_bs_call(allocate_pages,
					     EFI_ALLOCATE_ADDRESS,
					     EFI_LOADER_DATA,
					     (end - start) / EFI_PAGE_SIZE,
					     &start);
			if (status != EFI_SUCCESS) {
				efi_err("reserve_kernel_base(): alloc failed.\n");
				goto out;
			}
			break;

		case EFI_LOADER_CODE:
		case EFI_LOADER_DATA:
			/*
			 * These regions may be released and reallocated for
			 * another purpose (including EFI_RUNTIME_SERVICE_DATA)
			 * at any time during the execution of the OS loader,
			 * so we cannot consider them as safe.
			 */
		default:
			/*
			 * Treat any other allocation in the region as unsafe */
			status = EFI_OUT_OF_RESOURCES;
			goto out;
		}
	}

	status = EFI_SUCCESS;
out:
	efi_bs_call(free_pool, memory_map);
	return status;
}

efi_status_t handle_kernel_image(unsigned long *image_addr,
				 unsigned long *image_size,
				 unsigned long *reserve_addr,
				 unsigned long *reserve_size,
				 unsigned long dram_base,
				 efi_loaded_image_t *image)
{
	unsigned long kernel_base;
	efi_status_t status;

	/* use a 16 MiB aligned base for the decompressed kernel */
	kernel_base = round_up(dram_base, SZ_16M) + TEXT_OFFSET;

	/*
	 * Note that some platforms (notably, the Raspberry Pi 2) put
	 * spin-tables and other pieces of firmware at the base of RAM,
	 * abusing the fact that the window of TEXT_OFFSET bytes at the
	 * base of the kernel image is only partially used at the moment.
	 * (Up to 5 pages are used for the swapper page tables)
	 */
	status = reserve_kernel_base(kernel_base - 5 * PAGE_SIZE, reserve_addr,
				     reserve_size);
	if (status != EFI_SUCCESS) {
		efi_err("Unable to allocate memory for uncompressed kernel.\n");
		return status;
	}

	*image_addr = kernel_base;
	*image_size = 0;
	return EFI_SUCCESS;
}
