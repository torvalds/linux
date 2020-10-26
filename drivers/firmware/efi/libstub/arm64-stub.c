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
			efi_err("This 64 KB granular kernel is not supported by your CPU\n");
		else
			efi_err("This 16 KB granular kernel is not supported by your CPU\n");
		return EFI_UNSUPPORTED;
	}
	return EFI_SUCCESS;
}

/*
 * Although relocatable kernels can fix up the misalignment with respect to
 * MIN_KIMG_ALIGN, the resulting virtual text addresses are subtly out of
 * sync with those recorded in the vmlinux when kaslr is disabled but the
 * image required relocation anyway. Therefore retain 2M alignment unless
 * KASLR is in use.
 */
static u64 min_kimg_align(void)
{
	return efi_nokaslr ? MIN_KIMG_ALIGN : EFI_KIMG_ALIGN;
}

efi_status_t handle_kernel_image(unsigned long *image_addr,
				 unsigned long *image_size,
				 unsigned long *reserve_addr,
				 unsigned long *reserve_size,
				 efi_loaded_image_t *image)
{
	efi_status_t status;
	unsigned long kernel_size, kernel_memsize = 0;
	u32 phys_seed = 0;

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE)) {
		if (!efi_nokaslr) {
			status = efi_get_random_bytes(sizeof(phys_seed),
						      (u8 *)&phys_seed);
			if (status == EFI_NOT_FOUND) {
				efi_info("EFI_RNG_PROTOCOL unavailable, KASLR will be disabled\n");
				efi_nokaslr = true;
			} else if (status != EFI_SUCCESS) {
				efi_err("efi_get_random_bytes() failed (0x%lx), KASLR will be disabled\n",
					status);
				efi_nokaslr = true;
			}
		} else {
			efi_info("KASLR disabled on kernel command line\n");
		}
	}

	if (image->image_base != _text)
		efi_err("FIRMWARE BUG: efi_loaded_image_t::image_base has bogus value\n");

	kernel_size = _edata - _text;
	kernel_memsize = kernel_size + (_end - _edata);
	*reserve_size = kernel_memsize;

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && phys_seed != 0) {
		/*
		 * If KASLR is enabled, and we have some randomness available,
		 * locate the kernel at a randomized offset in physical memory.
		 */
		status = efi_random_alloc(*reserve_size, min_kimg_align(),
					  reserve_addr, phys_seed);
	} else {
		status = EFI_OUT_OF_RESOURCES;
	}

	if (status != EFI_SUCCESS) {
		if (IS_ALIGNED((u64)_text, min_kimg_align())) {
			/*
			 * Just execute from wherever we were loaded by the
			 * UEFI PE/COFF loader if the alignment is suitable.
			 */
			*image_addr = (u64)_text;
			*reserve_size = 0;
			return EFI_SUCCESS;
		}

		status = efi_allocate_pages_aligned(*reserve_size, reserve_addr,
						    ULONG_MAX, min_kimg_align());

		if (status != EFI_SUCCESS) {
			efi_err("Failed to relocate kernel\n");
			*reserve_size = 0;
			return status;
		}
	}

	*image_addr = *reserve_addr;
	memcpy((void *)*image_addr, _text, kernel_size);

	return EFI_SUCCESS;
}
