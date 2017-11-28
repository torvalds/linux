/*
 * XIP kernel .data segment decompressor
 *
 * Created by:	Nicolas Pitre, August 2017
 * Copyright:	(C) 2017  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/zutil.h>

/* for struct inflate_state */
#include "../../../lib/zlib_inflate/inftrees.h"
#include "../../../lib/zlib_inflate/inflate.h"
#include "../../../lib/zlib_inflate/infutil.h"

extern char __data_loc[];
extern char _edata_loc[];
extern char _sdata[];

/*
 * This code is called very early during the boot process to decompress
 * the .data segment stored compressed in ROM. Therefore none of the global
 * variables are valid yet, hence no kernel services such as memory
 * allocation is available. Everything must be allocated on the stack and
 * we must avoid any global data access. We use a temporary stack located
 * in the .bss area. The linker script makes sure the .bss is big enough
 * to hold our stack frame plus some room for called functions.
 *
 * We mimic the code in lib/decompress_inflate.c to use the smallest work
 * area possible. And because everything is statically allocated on the
 * stack then there is no need to clean up before returning.
 */

int __init __inflate_kernel_data(void)
{
	struct z_stream_s stream, *strm = &stream;
	struct inflate_state state;
	char *in = __data_loc;
	int rc;

	/* Check and skip gzip header (assume no filename) */
	if (in[0] != 0x1f || in[1] != 0x8b || in[2] != 0x08 || in[3] & ~3)
		return -1;
	in += 10;

	strm->workspace = &state;
	strm->next_in = in;
	strm->avail_in = _edata_loc - __data_loc;  /* upper bound */
	strm->next_out = _sdata;
	strm->avail_out = _edata_loc - __data_loc;
	zlib_inflateInit2(strm, -MAX_WBITS);
	WS(strm)->inflate_state.wsize = 0;
	WS(strm)->inflate_state.window = NULL;
	rc = zlib_inflate(strm, Z_FINISH);
	if (rc == Z_OK || rc == Z_STREAM_END)
		rc = strm->avail_out;  /* should be 0 */
	return rc;
}
