// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Wrapper around the kernel's pre-boot decompression library.
 *
 * Copyright (C) IBM Corporation 2016.
 */

#include "elf.h"
#include "page.h"
#include "string.h"
#include "stdio.h"
#include "ops.h"
#include "reg.h"
#include "types.h"

/*
 * The decompressor_*.c files play #ifdef games so they can be used in both
 * pre-boot and regular kernel code. We need these definitions to make the
 * includes work.
 */

#define STATIC static
#define INIT
#define __always_inline inline

/*
 * The build process will copy the required zlib source files and headers
 * out of lib/ and "fix" the includes so they do not pull in other kernel
 * headers.
 */

#ifdef CONFIG_KERNEL_GZIP
#	include "decompress_inflate.c"
#endif

#ifdef CONFIG_KERNEL_XZ
#	include "xz_config.h"
#	include "../../../lib/decompress_unxz.c"
#endif

/* globals for tracking the state of the decompression */
static unsigned long decompressed_bytes;
static unsigned long limit;
static unsigned long skip;
static char *output_buffer;

/*
 * flush() is called by __decompress() when the decompressor's scratch buffer is
 * full.
 */
static long flush(void *v, unsigned long buffer_size)
{
	unsigned long end = decompressed_bytes + buffer_size;
	unsigned long size = buffer_size;
	unsigned long offset = 0;
	char *in = v;
	char *out;

	/*
	 * if we hit our decompression limit, we need to fake an error to abort
	 * the in-progress decompression.
	 */
	if (decompressed_bytes >= limit)
		return -1;

	/* skip this entire block */
	if (end <= skip) {
		decompressed_bytes += buffer_size;
		return buffer_size;
	}

	/* skip some data at the start, but keep the rest of the block */
	if (decompressed_bytes < skip && end > skip) {
		offset = skip - decompressed_bytes;

		in += offset;
		size -= offset;
		decompressed_bytes += offset;
	}

	out = &output_buffer[decompressed_bytes - skip];
	size = min(decompressed_bytes + size, limit) - decompressed_bytes;

	memcpy(out, in, size);
	decompressed_bytes += size;

	return buffer_size;
}

static void print_err(char *s)
{
	/* suppress the "error" when we terminate the decompressor */
	if (decompressed_bytes >= limit)
		return;

	printf("Decompression error: '%s'\n\r", s);
}

/**
 * partial_decompress - decompresses part or all of a compressed buffer
 * @inbuf:       input buffer
 * @input_size:  length of the input buffer
 * @outbuf:      input buffer
 * @output_size: length of the input buffer
 * @skip         number of output bytes to ignore
 *
 * This function takes compressed data from inbuf, decompresses and write it to
 * outbuf. Once output_size bytes are written to the output buffer, or the
 * stream is exhausted the function will return the number of bytes that were
 * decompressed. Otherwise it will return whatever error code the decompressor
 * reported (NB: This is specific to each decompressor type).
 *
 * The skip functionality is mainly there so the program and discover
 * the size of the compressed image so that it can ask firmware (if present)
 * for an appropriately sized buffer.
 */
long partial_decompress(void *inbuf, unsigned long input_size,
	void *outbuf, unsigned long output_size, unsigned long _skip)
{
	int ret;

	/*
	 * The skipped bytes needs to be included in the size of data we want
	 * to decompress.
	 */
	output_size += _skip;

	decompressed_bytes = 0;
	output_buffer = outbuf;
	limit = output_size;
	skip = _skip;

	ret = __decompress(inbuf, input_size, NULL, flush, outbuf,
		output_size, NULL, print_err);

	/*
	 * If decompression was aborted due to an actual error rather than
	 * a fake error that we used to abort, then we should report it.
	 */
	if (decompressed_bytes < limit)
		return ret;

	return decompressed_bytes - skip;
}
