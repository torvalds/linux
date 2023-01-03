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

#include "efistub.h"

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
	u64 min_kimg_align = efi_get_kimg_min_align();

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
					  reserve_addr, phys_seed,
					  EFI_LOADER_CODE);
		if (status != EFI_SUCCESS)
			efi_warn("efi_random_alloc() failed: 0x%lx\n", status);
	} else {
		status = EFI_OUT_OF_RESOURCES;
	}

	if (status != EFI_SUCCESS) {
		if (!check_image_region((u64)_text, kernel_memsize)) {
			efi_err("FIRMWARE BUG: Image BSS overlaps adjacent EFI memory region\n");
		} else if (IS_ALIGNED((u64)_text, min_kimg_align) &&
			   (u64)_end < EFI_ALLOC_LIMIT) {
			/*
			 * Just execute from wherever we were loaded by the
			 * UEFI PE/COFF loader if the placement is suitable.
			 */
			*image_addr = (u64)_text;
			*reserve_size = 0;
			goto clean_image_to_poc;
		}

		status = efi_allocate_pages_aligned(*reserve_size, reserve_addr,
						    ULONG_MAX, min_kimg_align,
						    EFI_LOADER_CODE);

		if (status != EFI_SUCCESS) {
			efi_err("Failed to relocate kernel\n");
			*reserve_size = 0;
			return status;
		}
	}

	*image_addr = *reserve_addr;
	memcpy((void *)*image_addr, _text, kernel_size);

clean_image_to_poc:
	/*
	 * Clean the copied Image to the PoC, and ensure it is not shadowed by
	 * stale icache entries from before relocation.
	 */
	dcache_clean_poc(*image_addr, *image_addr + kernel_size);
	asm("ic ialluis");

	return EFI_SUCCESS;
}
