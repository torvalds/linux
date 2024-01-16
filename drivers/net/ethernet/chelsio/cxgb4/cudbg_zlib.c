// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2018 Chelsio Communications.  All rights reserved.
 */

#include <linux/zlib.h>

#include "cxgb4.h"
#include "cudbg_if.h"
#include "cudbg_lib_common.h"
#include "cudbg_zlib.h"

static int cudbg_get_compress_hdr(struct cudbg_buffer *pdbg_buff,
				  struct cudbg_buffer *pin_buff)
{
	if (pdbg_buff->offset + sizeof(struct cudbg_compress_hdr) >
	    pdbg_buff->size)
		return CUDBG_STATUS_NO_MEM;

	pin_buff->data = (char *)pdbg_buff->data + pdbg_buff->offset;
	pin_buff->offset = 0;
	pin_buff->size = sizeof(struct cudbg_compress_hdr);
	pdbg_buff->offset += sizeof(struct cudbg_compress_hdr);
	return 0;
}

int cudbg_compress_buff(struct cudbg_init *pdbg_init,
			struct cudbg_buffer *pin_buff,
			struct cudbg_buffer *pout_buff)
{
	struct cudbg_buffer temp_buff = { 0 };
	struct z_stream_s compress_stream;
	struct cudbg_compress_hdr *c_hdr;
	int rc;

	/* Write compression header to output buffer before compression */
	rc = cudbg_get_compress_hdr(pout_buff, &temp_buff);
	if (rc)
		return rc;

	c_hdr = (struct cudbg_compress_hdr *)temp_buff.data;
	c_hdr->compress_id = CUDBG_ZLIB_COMPRESS_ID;

	memset(&compress_stream, 0, sizeof(struct z_stream_s));
	compress_stream.workspace = pdbg_init->workspace;
	rc = zlib_deflateInit2(&compress_stream, Z_DEFAULT_COMPRESSION,
			       Z_DEFLATED, CUDBG_ZLIB_WIN_BITS,
			       CUDBG_ZLIB_MEM_LVL, Z_DEFAULT_STRATEGY);
	if (rc != Z_OK)
		return CUDBG_SYSTEM_ERROR;

	compress_stream.next_in = pin_buff->data;
	compress_stream.avail_in = pin_buff->size;
	compress_stream.next_out = pout_buff->data + pout_buff->offset;
	compress_stream.avail_out = pout_buff->size - pout_buff->offset;

	rc = zlib_deflate(&compress_stream, Z_FINISH);
	if (rc != Z_STREAM_END)
		return CUDBG_SYSTEM_ERROR;

	rc = zlib_deflateEnd(&compress_stream);
	if (rc != Z_OK)
		return CUDBG_SYSTEM_ERROR;

	c_hdr->compress_size = compress_stream.total_out;
	c_hdr->decompress_size = pin_buff->size;
	pout_buff->offset += compress_stream.total_out;

	return 0;
}
