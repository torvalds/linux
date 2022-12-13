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

	/*
	 * If we have 48 bits of VA space for TTBR0 mappings, we can map the
	 * UEFI runtime regions 1:1 and so calling SetVirtualAddressMap() is
	 * unnecessary.
	 */
	if (VA_BITS_MIN >= 48)
		efi_novamap = true;

	/* UEFI mandates support for 4 KB granularity, no need to check */
	if (IS_ENABLED(CONFIG_ARM64_4K_PAGES))
		return EFI_SUCCESS;

	tg = (read_cpuid(ID_AA64MMFR0_EL1) >> ID_AA64MMFR0_EL1_TGRAN_SHIFT) & 0xf;
	if (tg < ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MIN || tg > ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MAX) {
		if (IS_ENABLED(CONFIG_ARM64_64K_PAGES))
			efi_err("This 64 KB granular kernel is not supported by your CPU\n");
		else
			efi_err("This 16 KB granular kernel is not supported by your CPU\n");
		return EFI_UNSUPPORTED;
	}
	return EFI_SUCCESS;
}

/*
 * Distro versions of GRUB may ignore the BSS allocation entirely (i.e., fail
 * to provide space, and fail to zero it). Check for this condition by double
 * checking that the first and the last byte of the image are covered by the
 * same EFI memory map entry.
 */
static bool check_image_region(u64 base, u64 size)
{
	struct efi_boot_memmap *map;
	efi_status_t status;
	bool ret = false;
	int map_offset;

	status = efi_get_memory_map(&map, false);
	if (status != EFI_SUCCESS)
		return false;

	for (map_offset = 0; map_offset < map->map_size; map_offset += map->desc_size) {
		efi_memory_desc_t *md = (void *)map->map + map_offset;
		u64 end = md->phys_addr + md->num_pages * EFI_PAGE_SIZE;

		/*
		 * Find the region that covers base, and return whether
		 * it covers base+size bytes.
		 */
		if (base >= md->phys_addr && base < end) {
			ret = (base + size) <= end;
			break;
		}
	}

	efi_bs_call(free_pool, map);

	return ret;
}

efi_status_t handle_kernel_image(unsigned long *image_addr,
				 unsigned long *image_size,
				 unsigned long *reserve_addr,
				 unsigned long *reserve_size,
				 efi_loaded_image_t *image,
				 efi_handle_t image_handle)
{
	efi_status_t status;
	unsigned long kernel_size, kernel_memsize = 0;
	u32 phys_seed = 0;

	/*
	 * Although relocatable kernels can fix up the misalignment with
	 * respect to MIN_KIMG_ALIGN, the resulting virtual text addresses are
	 * subtly out of sync with those recorded in the vmlinux when kaslr is
	 * disabled but the image required relocation anyway. Therefore retain
	 * 2M alignment if KASLR was explicitly disabled, even if it was not
	 * going to be activated to begin with.
	 */
	u64 min_kimg_align = efi_nokaslr ? MIN_KIMG_ALIGN : EFI_KIMG_ALIGN;

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE)) {
		efi_guid_t li_fixed_proto = LINUX_EFI_LOADED_IMAGE_FIXED_GUID;
		void *p;

		if (efi_nokaslr) {
			efi_info("KASLR disabled on kernel command line\n");
		} else if (efi_bs_call(handle_protocol, image_handle,
				       &li_fixed_proto, &p) == EFI_SUCCESS) {
			efi_info("Image placement fixed by loader\n");
		} else {
			status = efi_get_random_bytes(sizeof(phys_seed),
						      (u8 *)&phys_seed);
			if (status == EFI_NOT_FOUND) {
				efi_info("EFI_RNG_PROTOCOL unavailable\n");
				efi_nokaslr = true;
			} else if (status != EFI_SUCCESS) {
				efi_err("efi_get_random_bytes() failed (0x%lx)\n",
					status);
				efi_nokaslr = true;
			}
		}
	}

	if (image->image_base != _text)
		efi_err("FIRMWARE BUG: efi_loaded_image_t::image_base has bogus value\n");

	if (!IS_ALIGNED((u64)_text, SEGMENT_ALIGN))
		efi_err("FIRMWARE BUG: kernel image not aligned on %dk boundary\n",
			SEGMENT_ALIGN >> 10);

	kernel_size = _edata - _text;
	kernel_memsize = kernel_size + (_end - _edata);
	*reserve_size = kernel_memsize;

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && phys_seed != 0) {
		/*
		 * If KASLR is enabled, and we have some randomness available,
		 * locate the kernel at a randomized offset in physical memory.
		 */
		status = efi_random_alloc(*reserve_size, min_kimg_align,
					  reserve_addr, phys_seed);
		if (status != EFI_SUCCESS)
			efi_warn("efi_random_alloc() failed: 0x%lx\n", status);
	} else {
		status = EFI_OUT_OF_RESOURCES;
	}

	if (status != EFI_SUCCESS) {
		if (!check_image_region((u64)_text, kernel_memsize)) {
			efi_err("FIRMWARE BUG: Image BSS overlaps adjacent EFI memory region\n");
		} else if (IS_ALIGNED((u64)_text, min_kimg_align)) {
			/*
			 * Just execute from wherever we were loaded by the
			 * UEFI PE/COFF loader if the alignment is suitable.
			 */
			*image_addr = (u64)_text;
			*reserve_size = 0;
			return EFI_SUCCESS;
		}

		status = efi_allocate_pages_aligned(*reserve_size, reserve_addr,
						    ULONG_MAX, min_kimg_align);

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
