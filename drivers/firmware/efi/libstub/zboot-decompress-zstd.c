// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <linux/zstd.h>

#include <asm/efi.h>

#include "decompress_sources.h"
#include "efistub.h"

extern unsigned char _gzdata_start[], _gzdata_end[];
extern u32 __aligned(1) payload_size;

static size_t wksp_size;
static void *wksp;

efi_status_t efi_zboot_decompress_init(unsigned long *alloc_size)
{
	efi_status_t status;

	wksp_size = zstd_dctx_workspace_bound();
	status = efi_allocate_pages(wksp_size, (unsigned long *)&wksp, ULONG_MAX);
	if (status != EFI_SUCCESS)
		return status;

	*alloc_size = payload_size;
	return EFI_SUCCESS;
}

efi_status_t efi_zboot_decompress(u8 *out, unsigned long outlen)
{
	zstd_dctx *dctx = zstd_init_dctx(wksp, wksp_size);
	size_t ret;
	int retval;

	ret = zstd_decompress_dctx(dctx, out, outlen, _gzdata_start,
				   _gzdata_end - _gzdata_start - 4);
	efi_free(wksp_size, (unsigned long)wksp);

	retval = zstd_get_error_code(ret);
	if (retval) {
		efi_err("ZSTD-decompression failed with status %d\n", retval);
		return EFI_LOAD_ERROR;
	}

	efi_cache_sync_image((unsigned long)out, outlen);

	return EFI_SUCCESS;
}
