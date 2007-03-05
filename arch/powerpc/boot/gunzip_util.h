/*
 * Decompression convenience functions
 *
 * Copyright 2007 David Gibson, IBM Corporation.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _PPC_BOOT_GUNZIP_UTIL_H_
#define _PPC_BOOT_GUNZIP_UTIL_H_

#include "zlib.h"

/* scratch space for gunzip; 46912 is from zlib_inflate_workspacesize() */
#define GUNZIP_SCRATCH_SIZE	46912

struct gunzip_state {
	z_stream s;
	char scratch[46912];
};

void gunzip_start(struct gunzip_state *state, void *src, int srclen);
int gunzip_partial(struct gunzip_state *state, void *dst, int dstlen);
void gunzip_exactly(struct gunzip_state *state, void *dst, int len);
void gunzip_discard(struct gunzip_state *state, int len);
int gunzip_finish(struct gunzip_state *state, void *dst, int len);

#endif /* _PPC_BOOT_GUNZIP_UTIL_H_ */

