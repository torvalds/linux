// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <linux/zlib.h>

#include <asm/efi.h>

#include "efistub.h"

#include "inftrees.c"
#include "inffast.c"
#include "inflate.c"

extern unsigned char _gzdata_start[], _gzdata_end[];
extern u32 __aligned(1) payload_size;

static struct z_stream_s stream;

efi_status_t efi_zboot_decompress_init(unsigned long *alloc_size)
{
	efi_status_t status;
	int rc;

	/* skip the 10 byte header, assume no recorded filename */
	stream.next_in = _gzdata_start + 10;
	stream.avail_in = _gzdata_end - stream.next_in;

	status = efi_allocate_pages(zlib_inflate_workspacesize(),
				    (unsigned long *)&stream.workspace,
				    ULONG_MAX);
	if (status != EFI_SUCCESS)
		return status;

	rc = zlib_inflateInit2(&stream, -MAX_WBITS);
	if (rc != Z_OK) {
		efi_err("failed to initialize GZIP decompressor: %d\n", rc);
		status = EFI_LOAD_ERROR;
		goto out;
	}

	*alloc_size = payload_size;
	return EFI_SUCCESS;
out:
	efi_free(zlib_inflate_workspacesize(), (unsigned long)stream.workspace);
	return status;
}

efi_status_t efi_zboot_decompress(u8 *out, unsigned long outlen)
{
	int rc;

	stream.next_out = out;
	stream.avail_out = outlen;

	rc = zlib_inflate(&stream, 0);
	zlib_inflateEnd(&stream);

	efi_free(zlib_inflate_workspacesize(), (unsigned long)stream.workspace);

	if (rc != Z_STREAM_END) {
		efi_err("GZIP decompression failed with status %d\n", rc);
		return EFI_LOAD_ERROR;
	}

	efi_cache_sync_image((unsigned long)out, outlen);

	return EFI_SUCCESS;
}
