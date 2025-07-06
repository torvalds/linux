// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <linux/pe.h>
#include <asm/efi.h>
#include <linux/unaligned.h>

#include "efistub.h"

static unsigned long alloc_preferred_address(unsigned long alloc_size)
{
#ifdef EFI_KIMG_PREFERRED_ADDRESS
	efi_physical_addr_t efi_addr = EFI_KIMG_PREFERRED_ADDRESS;

	if (efi_bs_call(allocate_pages, EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
			alloc_size / EFI_PAGE_SIZE, &efi_addr) == EFI_SUCCESS)
		return efi_addr;
#endif
	return ULONG_MAX;
}

void __weak efi_cache_sync_image(unsigned long image_base,
				 unsigned long alloc_size)
{
	// Provided by the arch to perform the cache maintenance necessary for
	// executable code loaded into memory to be safe for execution.
}

struct screen_info *alloc_screen_info(void)
{
	return __alloc_screen_info();
}

asmlinkage efi_status_t __efiapi
efi_zboot_entry(efi_handle_t handle, efi_system_table_t *systab)
{
	char *cmdline_ptr __free(efi_pool) = NULL;
	unsigned long image_base, alloc_size;
	efi_loaded_image_t *image;
	efi_status_t status;

	WRITE_ONCE(efi_system_table, systab);

	status = efi_bs_call(handle_protocol, handle,
			     &LOADED_IMAGE_PROTOCOL_GUID, (void **)&image);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to locate parent's loaded image protocol\n");
		return status;
	}

	status = efi_handle_cmdline(image, &cmdline_ptr);
	if (status != EFI_SUCCESS)
		return status;

	efi_info("Decompressing Linux Kernel...\n");

	status = efi_zboot_decompress_init(&alloc_size);
	if (status != EFI_SUCCESS)
		return status;

	 // If the architecture has a preferred address for the image,
	 // try that first.
	image_base = alloc_preferred_address(alloc_size);
	if (image_base == ULONG_MAX) {
		unsigned long min_kimg_align = efi_get_kimg_min_align();
		u32 seed = U32_MAX;

		if (!IS_ENABLED(CONFIG_RANDOMIZE_BASE)) {
			// Setting the random seed to 0x0 is the same as
			// allocating as low as possible
			seed = 0;
		} else if (efi_nokaslr) {
			efi_info("KASLR disabled on kernel command line\n");
		} else {
			status = efi_get_random_bytes(sizeof(seed), (u8 *)&seed);
			if (status == EFI_NOT_FOUND) {
				efi_info("EFI_RNG_PROTOCOL unavailable\n");
				efi_nokaslr = true;
			} else if (status != EFI_SUCCESS) {
				efi_err("efi_get_random_bytes() failed (0x%lx)\n",
					status);
				efi_nokaslr = true;
			}
		}

		status = efi_random_alloc(alloc_size, min_kimg_align, &image_base,
					  seed, EFI_LOADER_CODE, 0, EFI_ALLOC_LIMIT);
		if (status != EFI_SUCCESS) {
			efi_err("Failed to allocate memory\n");
			return status;
		}
	}

	// Decompress the payload into the newly allocated buffer
	status = efi_zboot_decompress((void *)image_base, alloc_size) ?:
	         efi_stub_common(handle, image, image_base, cmdline_ptr);

	efi_free(alloc_size, image_base);
	return status;
}
