// SPDX-License-Identifier: GPL-2.0
/*
 * Helper functions used by the EFI stub on multiple
 * architectures to deal with physical address space randomization.
 */
#include <linux/efi.h>

#include "efistub.h"

/**
 * efi_kaslr_get_phys_seed() - Get random seed for physical kernel KASLR
 * @image_handle:	Handle to the image
 *
 * If KASLR is not disabled, obtain a random seed using EFI_RNG_PROTOCOL
 * that will be used to move the kernel physical mapping.
 *
 * Return:	the random seed
 */
u32 efi_kaslr_get_phys_seed(efi_handle_t image_handle)
{
	efi_guid_t li_fixed_proto = LINUX_EFI_LOADED_IMAGE_FIXED_GUID;
	void *p;

	if (!IS_ENABLED(CONFIG_RANDOMIZE_BASE))
		return 0;

	if (efi_nokaslr) {
		efi_info("KASLR disabled on kernel command line\n");
	} else if (efi_bs_call(handle_protocol, image_handle,
			       &li_fixed_proto, &p) == EFI_SUCCESS) {
		efi_info("Image placement fixed by loader\n");
	} else {
		efi_status_t status;
		u32 phys_seed;

		status = efi_get_random_bytes(sizeof(phys_seed),
					      (u8 *)&phys_seed);
		if (status == EFI_SUCCESS)
			return phys_seed;

		if (status == EFI_NOT_FOUND)
			efi_info("EFI_RNG_PROTOCOL unavailable\n");
		else
			efi_err("efi_get_random_bytes() failed (0x%lx)\n", status);

		efi_nokaslr = true;
	}

	return 0;
}

/*
 * Distro versions of GRUB may ignore the BSS allocation entirely (i.e., fail
 * to provide space, and fail to zero it). Check for this condition by double
 * checking that the first and the last byte of the image are covered by the
 * same EFI memory map entry.
 */
static bool check_image_region(u64 base, u64 size)
{
	struct efi_boot_memmap *map __free(efi_pool) = NULL;
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

	return ret;
}

/**
 * efi_kaslr_relocate_kernel() - Relocate the kernel (random if KASLR enabled)
 * @image_addr: Pointer to the current kernel location
 * @reserve_addr:	Pointer to the relocated kernel location
 * @reserve_size:	Size of the relocated kernel
 * @kernel_size:	Size of the text + data
 * @kernel_codesize:	Size of the text
 * @kernel_memsize:	Size of the text + data + bss
 * @phys_seed:		Random seed used for the relocation
 *
 * If KASLR is not enabled, this function relocates the kernel to a fixed
 * address (or leave it as its current location). If KASLR is enabled, the
 * kernel physical location is randomized using the seed in parameter.
 *
 * Return:	status code, EFI_SUCCESS if relocation is successful
 */
efi_status_t efi_kaslr_relocate_kernel(unsigned long *image_addr,
				       unsigned long *reserve_addr,
				       unsigned long *reserve_size,
				       unsigned long kernel_size,
				       unsigned long kernel_codesize,
				       unsigned long kernel_memsize,
				       u32 phys_seed)
{
	efi_status_t status;
	u64 min_kimg_align = efi_get_kimg_min_align();

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && phys_seed != 0) {
		/*
		 * If KASLR is enabled, and we have some randomness available,
		 * locate the kernel at a randomized offset in physical memory.
		 */
		status = efi_random_alloc(*reserve_size, min_kimg_align,
					  reserve_addr, phys_seed,
					  EFI_LOADER_CODE, 0, EFI_ALLOC_LIMIT);
		if (status != EFI_SUCCESS)
			efi_warn("efi_random_alloc() failed: 0x%lx\n", status);
	} else {
		status = EFI_OUT_OF_RESOURCES;
	}

	if (status != EFI_SUCCESS) {
		if (!check_image_region(*image_addr, kernel_memsize)) {
			efi_err("FIRMWARE BUG: Image BSS overlaps adjacent EFI memory region\n");
		} else if (IS_ALIGNED(*image_addr, min_kimg_align) &&
			   (unsigned long)_end < EFI_ALLOC_LIMIT) {
			/*
			 * Just execute from wherever we were loaded by the
			 * UEFI PE/COFF loader if the placement is suitable.
			 */
			*reserve_size = 0;
			return EFI_SUCCESS;
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

	memcpy((void *)*reserve_addr, (void *)*image_addr, kernel_size);
	*image_addr = *reserve_addr;
	efi_icache_sync(*image_addr, *image_addr + kernel_codesize);
	efi_remap_image(*image_addr, *reserve_size, kernel_codesize);

	return status;
}
