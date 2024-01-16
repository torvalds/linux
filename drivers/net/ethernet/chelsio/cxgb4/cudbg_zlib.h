/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2018 Chelsio Communications.  All rights reserved.
 */

#ifndef __CUDBG_ZLIB_H__
#define __CUDBG_ZLIB_H__

#include <linux/zlib.h>

#define CUDBG_ZLIB_COMPRESS_ID 17
#define CUDBG_ZLIB_WIN_BITS 12
#define CUDBG_ZLIB_MEM_LVL 4

struct cudbg_compress_hdr {
	u32 compress_id;
	u64 decompress_size;
	u64 compress_size;
	u64 rsvd[32];
};

static inline int cudbg_get_workspace_size(void)
{
	return zlib_deflate_workspacesize(CUDBG_ZLIB_WIN_BITS,
					  CUDBG_ZLIB_MEM_LVL);
}

int cudbg_compress_buff(struct cudbg_init *pdbg_init,
			struct cudbg_buffer *pin_buff,
			struct cudbg_buffer *pout_buff);
#endif /* __CUDBG_ZLIB_H__ */
