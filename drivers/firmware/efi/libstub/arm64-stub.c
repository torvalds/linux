// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013, 2014 Linaro Ltd;  <roy.franz@linaro.org>
 *
 * This file implements the EFI boot stub for the arm64 kernel.
 * Adapted from ARM version by Mark Salter <msalter@redhat.com>
 */


#include <linux/efi.h>
#include <asm/efi.h>
#include <asm/memory.h>
#include <asm/sections.h>
#include <asm/sysreg.h>

#include "efistub.h"

efi_status_t check_platform_features(void)
{
	u64 tg;

	/* UEFI mandates support for 4 KB granularity, no need to check */
	if (IS_ENABLED(CONFIG_ARM64_4K_PAGES))
		return EFI_SUCCESS;

	tg = (read_cpuid(ID_AA64MMFR0_EL1) >> ID_AA64MMFR0_TGRAN_SHIFT) & 0xf;
	if (tg != ID_AA64MMFR0_TGRAN_SUPPORTED) {
		if (IS_ENABLED(CONFIG_ARM64_64K_PAGES))
			pr_efi_err("This 64 KB granular kernel is not supported by your CPU\n");
		else
			pr_efi_err("This 16 KB granular kernel is not supported by your CPU\n");
		return EFI_UNSUPPORTED;
	}
	return EFI_SUCCESS;
}

/*
 * Relocatable kernels can fix up the misalignment with respect to
 * MIN_KIMG_ALIGN, so they only require a minimum alignment of EFI_KIMG_ALIGN
 * (which accounts for the alignment of statically allocated objects such as
 * the swapper stack.)
 */
static const u64 min_kimg_align = IS_ENABLED(CONFIG_RELOCATABLE) ? EFI_KIMG_ALIGN
								 : MIN_KIMG_ALIGN;

efi_status_t handle_kernel_image(unsigned long *image_addr,
				 unsigned long *image_size,
				 unsigned long *reserve_addr,
				 unsigned long *reserve_size,
				 unsigned long dram_base,
				 efi_loaded_image_t *image)
{
	efi_status_t status;
	unsigned long kernel_size, kernel_memsize = 0;
	u64 phys_seed = 0;

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE)) {
		if (!nokaslr()) {
			status = efi_get_random_bytes(sizeof(phys_seed),
						      (u8 *)&phys_seed);
			if (status == EFI_NOT_FOUND) {
				pr_efi("EFI_RNG_PROTOCOL unavailable, no randomness supplied\n");
			} else if (status != EFI_SUCCESS) {
				pr_efi_err("efi_get_random_bytes() failed\n");
				return status;
			}
		} else {
			pr_efi("KASLR disabled on kernel command line\n");
		}
	}

	if (image->image_base != _text)
		pr_efi_err("FIRMWARE BUG: efi_loaded_image_t::image_base has bogus value\n");

	kernel_size = _edata - _text;
	kernel_memsize = kernel_size + (_end - _edata);

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && phys_seed != 0) {
		/*
		 * Produce a displacement in the interval [0, MIN_KIMG_ALIGN)
		 * that doesn't violate this kernel's de-facto alignment
		 * constraints.
		 */
		u32 mask = (MIN_KIMG_ALIGN - 1) & ~(EFI_KIMG_ALIGN - 1);
		u32 offset = (phys_seed >> 32) & mask;

		/*
		 * With CONFIG_RANDOMIZE_TEXT_OFFSET=y, TEXT_OFFSET may not
		 * be a multiple of EFI_KIMG_ALIGN, and we must ensure that
		 * we preserve the misalignment of 'offset' relative to
		 * EFI_KIMG_ALIGN so that statically allocated objects whose
		 * alignment exceeds PAGE_SIZE appear correctly aligned in
		 * memory.
		 */
		offset |= TEXT_OFFSET % EFI_KIMG_ALIGN;

		/*
		 * If KASLR is enabled, and we have some randomness available,
		 * locate the kernel at a randomized offset in physical memory.
		 */
		*reserve_size = kernel_memsize + offset;
		status = efi_random_alloc(*reserve_size,
					  MIN_KIMG_ALIGN, reserve_addr,
					  (u32)phys_seed);

		*image_addr = *reserve_addr + offset;
	} else {
		status = EFI_OUT_OF_RESOURCES;
	}

	if (status != EFI_SUCCESS) {
		if (IS_ALIGNED((u64)_text - TEXT_OFFSET, min_kimg_align)) {
			/*
			 * Just execute from wherever we were loaded by the
			 * UEFI PE/COFF loader if the alignment is suitable.
			 */
			*image_addr = (u64)_text;
			*reserve_size = 0;
			return EFI_SUCCESS;
		}

		*reserve_size = kernel_memsize + TEXT_OFFSET % min_kimg_align;
		status = efi_low_alloc(*reserve_size,
				       min_kimg_align, reserve_addr);

		if (status != EFI_SUCCESS) {
			pr_efi_err("Failed to relocate kernel\n");
			*reserve_size = 0;
			return status;
		}
		*image_addr = *reserve_addr + TEXT_OFFSET % min_kimg_align;
	}

	memcpy((void *)*image_addr, _text, kernel_size);

	return EFI_SUCCESS;
}
