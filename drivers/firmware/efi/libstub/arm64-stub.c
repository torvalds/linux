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

/*
 * To prevent the compiler from emitting GOT-indirected (and thus absolute)
 * references to the section markers, override their visibility as 'hidden'
 */
#pragma GCC visibility push(hidden)
#include <asm/sections.h>
#pragma GCC visibility pop

#include <linux/efi.h>
#include <asm/efi.h>
#include <asm/memory.h>
#include <asm/sysreg.h>

#include "efistub.h"

efi_status_t check_platform_features(efi_system_table_t *sys_table_arg)
{
	u64 tg;

	/* UEFI mandates support for 4 KB granularity, no need to check */
	if (IS_ENABLED(CONFIG_ARM64_4K_PAGES))
		return EFI_SUCCESS;

	tg = (read_cpuid(ID_AA64MMFR0_EL1) >> ID_AA64MMFR0_TGRAN_SHIFT) & 0xf;
	if (tg != ID_AA64MMFR0_TGRAN_SUPPORTED) {
		if (IS_ENABLED(CONFIG_ARM64_64K_PAGES))
			pr_efi_err(sys_table_arg, "This 64 KB granular kernel is not supported by your CPU\n");
		else
			pr_efi_err(sys_table_arg, "This 16 KB granular kernel is not supported by your CPU\n");
		return EFI_UNSUPPORTED;
	}
	return EFI_SUCCESS;
}

efi_status_t handle_kernel_image(efi_system_table_t *sys_table_arg,
				 unsigned long *image_addr,
				 unsigned long *image_size,
				 unsigned long *reserve_addr,
				 unsigned long *reserve_size,
				 unsigned long dram_base,
				 efi_loaded_image_t *image)
{
	efi_status_t status;
	unsigned long kernel_size, kernel_memsize = 0;
	void *old_image_addr = (void *)*image_addr;
	unsigned long preferred_offset;
	u64 phys_seed = 0;

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE)) {
		if (!nokaslr()) {
			status = efi_get_random_bytes(sys_table_arg,
						      sizeof(phys_seed),
						      (u8 *)&phys_seed);
			if (status == EFI_NOT_FOUND) {
				pr_efi(sys_table_arg, "EFI_RNG_PROTOCOL unavailable, no randomness supplied\n");
			} else if (status != EFI_SUCCESS) {
				pr_efi_err(sys_table_arg, "efi_get_random_bytes() failed\n");
				return status;
			}
		} else {
			pr_efi(sys_table_arg, "KASLR disabled on kernel command line\n");
		}
	}

	/*
	 * The preferred offset of the kernel Image is TEXT_OFFSET bytes beyond
	 * a 2 MB aligned base, which itself may be lower than dram_base, as
	 * long as the resulting offset equals or exceeds it.
	 */
	preferred_offset = round_down(dram_base, MIN_KIMG_ALIGN) + TEXT_OFFSET;
	if (preferred_offset < dram_base)
		preferred_offset += MIN_KIMG_ALIGN;

	kernel_size = _edata - _text;
	kernel_memsize = kernel_size + (_end - _edata);

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && phys_seed != 0) {
		/*
		 * If CONFIG_DEBUG_ALIGN_RODATA is not set, produce a
		 * displacement in the interval [0, MIN_KIMG_ALIGN) that
		 * doesn't violate this kernel's de-facto alignment
		 * constraints.
		 */
		u32 mask = (MIN_KIMG_ALIGN - 1) & ~(EFI_KIMG_ALIGN - 1);
		u32 offset = !IS_ENABLED(CONFIG_DEBUG_ALIGN_RODATA) ?
			     (phys_seed >> 32) & mask : TEXT_OFFSET;

		/*
		 * If KASLR is enabled, and we have some randomness available,
		 * locate the kernel at a randomized offset in physical memory.
		 */
		*reserve_size = kernel_memsize + offset;
		status = efi_random_alloc(sys_table_arg, *reserve_size,
					  MIN_KIMG_ALIGN, reserve_addr,
					  (u32)phys_seed);

		*image_addr = *reserve_addr + offset;
	} else {
		/*
		 * Else, try a straight allocation at the preferred offset.
		 * This will work around the issue where, if dram_base == 0x0,
		 * efi_low_alloc() refuses to allocate at 0x0 (to prevent the
		 * address of the allocation to be mistaken for a FAIL return
		 * value or a NULL pointer). It will also ensure that, on
		 * platforms where the [dram_base, dram_base + TEXT_OFFSET)
		 * interval is partially occupied by the firmware (like on APM
		 * Mustang), we can still place the kernel at the address
		 * 'dram_base + TEXT_OFFSET'.
		 */
		if (*image_addr == preferred_offset)
			return EFI_SUCCESS;

		*image_addr = *reserve_addr = preferred_offset;
		*reserve_size = round_up(kernel_memsize, EFI_ALLOC_ALIGN);

		status = efi_call_early(allocate_pages, EFI_ALLOCATE_ADDRESS,
					EFI_LOADER_DATA,
					*reserve_size / EFI_PAGE_SIZE,
					(efi_physical_addr_t *)reserve_addr);
	}

	if (status != EFI_SUCCESS) {
		*reserve_size = kernel_memsize + TEXT_OFFSET;
		status = efi_low_alloc(sys_table_arg, *reserve_size,
				       MIN_KIMG_ALIGN, reserve_addr);

		if (status != EFI_SUCCESS) {
			pr_efi_err(sys_table_arg, "Failed to relocate kernel\n");
			*reserve_size = 0;
			return status;
		}
		*image_addr = *reserve_addr + TEXT_OFFSET;
	}
	memcpy((void *)*image_addr, old_image_addr, kernel_size);

	return EFI_SUCCESS;
}
