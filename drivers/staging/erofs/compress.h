/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/drivers/staging/erofs/compress.h
 *
 * Copyright (C) 2019 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#ifndef __EROFS_FS_COMPRESS_H
#define __EROFS_FS_COMPRESS_H

#include "internal.h"

enum {
	Z_EROFS_COMPRESSION_SHIFTED = Z_EROFS_COMPRESSION_MAX,
	Z_EROFS_COMPRESSION_RUNTIME_MAX
};

struct z_erofs_decompress_req {
	struct page **in, **out;

	unsigned short pageofs_out;
	unsigned int inputsize, outputsize;

	/* indicate the algorithm will be used for decompression */
	unsigned int alg;
	bool inplace_io, partial_decoding;
};

/*
 * - 0x5A110C8D ('sallocated', Z_EROFS_MAPPING_STAGING) -
 * used to mark temporary allocated pages from other
 * file/cached pages and NULL mapping pages.
 */
#define Z_EROFS_MAPPING_STAGING         ((void *)0x5A110C8D)

/* check if a page is marked as staging */
static inline bool z_erofs_page_is_staging(struct page *page)
{
	return page->mapping == Z_EROFS_MAPPING_STAGING;
}

static inline bool z_erofs_put_stagingpage(struct list_head *pagepool,
					   struct page *page)
{
	if (!z_erofs_page_is_staging(page))
		return false;

	/* staging pages should not be used by others at the same time */
	if (page_ref_count(page) > 1)
		put_page(page);
	else
		list_add(&page->lru, pagepool);
	return true;
}

int z_erofs_decompress(struct z_erofs_decompress_req *rq,
		       struct list_head *pagepool);

#endif

