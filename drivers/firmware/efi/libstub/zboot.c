// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <linux/pe.h>
#include <asm/efi.h>
#include <linux/unaligned.h>

#include "efistub.h"

static unsigned char zboot_heap[SZ_256K] __aligned(64);
static unsigned long free_mem_ptr, free_mem_end_ptr;

#define STATIC static
#if defined(CONFIG_KERNEL_GZIP)
#include "../../../../lib/decompress_inflate.c"
#elif defined(CONFIG_KERNEL_LZ4)
#include "../../../../lib/decompress_unlz4.c"
#elif defined(CONFIG_KERNEL_LZMA)
#include "../../../../lib/decompress_unlzma.c"
#elif defined(CONFIG_KERNEL_LZO)
#include "../../../../lib/decompress_unlzo.c"
#elif defined(CONFIG_KERNEL_XZ)
#undef memcpy
#define memcpy memcpy
#undef memmove
#define memmove memmove
#include "../../../../lib/decompress_unxz.c"
#elif defined(CONFIG_KERNEL_ZSTD)
#include "../../../../lib/decompress_unzstd.c"
#endif

extern char efi_zboot_header[];
extern char _gzdata_start[], _gzdata_end[];

static void error(char *x)
{
	efi_err("EFI decompressor: %s\n", x);
}

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
	unsigned long compressed_size = _gzdata_end - _gzdata_start;
	unsigned long image_base, alloc_size;
	efi_loaded_image_t *image;
	efi_status_t status;
	char *cmdline_ptr;
	int ret;

	WRITE_ONCE(efi_system_table, systab);

	free_mem_ptr = (unsigned long)&zboot_heap;
	free_mem_end_ptr = free_mem_ptr + sizeof(zboot_heap);

	status = efi_bs_call(handle_protocol, handle,
			     &LOADED_IMAGE_PROTOCOL_GUID, (void **)&image);
	if (status != EFI_SUCCESS) {
		error("Failed to locate parent's loaded image protocol");
		return status;
	}

	status = efi_handle_cmdline(image, &cmdline_ptr);
	if (status != EFI_SUCCESS)
		return status;

	efi_info("Decompressing Linux Kernel...\n");

	// SizeOfImage from the compressee's PE/COFF header
	alloc_size = round_up(get_unaligned_le32(_gzdata_end - 4),
			      EFI_ALLOC_ALIGN);

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
			goto free_cmdline;
		}
	}

	// Decompress the payload into the newly allocated buffer.
	ret = __decompress(_gzdata_start, compressed_size, NULL, NULL,
			   (void *)image_base, alloc_size, NULL, error);
	if (ret	< 0) {
		error("Decompression failed");
		status = EFI_DEVICE_ERROR;
		goto free_image;
	}

	efi_cache_sync_image(image_base, alloc_size);

	status = efi_stub_common(handle, image, image_base, cmdline_ptr);

free_image:
	efi_free(alloc_size, image_base);
free_cmdline:
	efi_bs_call(free_pool, cmdline_ptr);
	return status;
}
