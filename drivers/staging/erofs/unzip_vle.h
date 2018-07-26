/* SPDX-License-Identifier: GPL-2.0
 *
 * linux/drivers/staging/erofs/unzip_vle.h
 *
 * Copyright (C) 2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#ifndef __EROFS_FS_UNZIP_VLE_H
#define __EROFS_FS_UNZIP_VLE_H

#include "internal.h"

#define Z_EROFS_VLE_INLINE_PAGEVECS     3

/* unzip_vle_lz4.c */
extern int z_erofs_vle_plain_copy(struct page **compressed_pages,
	unsigned clusterpages, struct page **pages,
	unsigned nr_pages, unsigned short pageofs);

extern int z_erofs_vle_unzip_fast_percpu(struct page **compressed_pages,
	unsigned clusterpages, struct page **pages,
	unsigned outlen, unsigned short pageofs,
	void (*endio)(struct page *));

extern int z_erofs_vle_unzip_vmap(struct page **compressed_pages,
	unsigned clusterpages, void *vaddr, unsigned llen,
	unsigned short pageofs, bool overlapped);

#endif

