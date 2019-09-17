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
#include "unzip_pagevec.h"

#define Z_EROFS_NR_INLINE_PAGEVECS      3

/*
 * Structure fields follow one of the following exclusion rules.
 *
 * I: Modifiable by initialization/destruction paths and read-only
 *    for everyone else.
 *
 */

struct z_erofs_vle_work {
	struct mutex lock;

	/* I: decompression offset in page */
	unsigned short pageofs;
	unsigned short nr_pages;

	/* L: queued pages in pagevec[] */
	unsigned vcnt;

	union {
		/* L: pagevec */
		erofs_vtptr_t pagevec[Z_EROFS_NR_INLINE_PAGEVECS];
		struct rcu_head rcu;
	};
};

#define Z_EROFS_VLE_WORKGRP_FMT_PLAIN        0
#define Z_EROFS_VLE_WORKGRP_FMT_LZ4          1
#define Z_EROFS_VLE_WORKGRP_FMT_MASK         1
#define Z_EROFS_VLE_WORKGRP_FULL_LENGTH      2

typedef void *z_erofs_vle_owned_workgrp_t;

struct z_erofs_vle_workgroup {
	struct erofs_workgroup obj;
	struct z_erofs_vle_work work;

	/* point to next owned_workgrp_t */
	z_erofs_vle_owned_workgrp_t next;

	/* compressed pages (including multi-usage pages) */
	struct page *compressed_pages[Z_EROFS_CLUSTER_MAX_PAGES];
	unsigned int llen, flags;
};

/* let's avoid the valid 32-bit kernel addresses */

/* the chained workgroup has't submitted io (still open) */
#define Z_EROFS_VLE_WORKGRP_TAIL        ((void *)0x5F0ECAFE)
/* the chained workgroup has already submitted io */
#define Z_EROFS_VLE_WORKGRP_TAIL_CLOSED ((void *)0x5F0EDEAD)

#define Z_EROFS_VLE_WORKGRP_NIL         (NULL)

#define z_erofs_vle_workgrp_fmt(grp)	\
	((grp)->flags & Z_EROFS_VLE_WORKGRP_FMT_MASK)

static inline void z_erofs_vle_set_workgrp_fmt(
	struct z_erofs_vle_workgroup *grp,
	unsigned int fmt)
{
	grp->flags = fmt | (grp->flags & ~Z_EROFS_VLE_WORKGRP_FMT_MASK);
}


/* definitions if multiref is disabled */
#define z_erofs_vle_grab_primary_work(grp)	(&(grp)->work)
#define z_erofs_vle_grab_work(grp, pageofs)	(&(grp)->work)
#define z_erofs_vle_work_workgroup(wrk, primary)	\
	((primary) ? container_of(wrk,	\
		struct z_erofs_vle_workgroup, work) : \
		({ BUG(); (void *)NULL; }))


#define Z_EROFS_WORKGROUP_SIZE       sizeof(struct z_erofs_vle_workgroup)

struct z_erofs_vle_unzip_io {
	atomic_t pending_bios;
	z_erofs_vle_owned_workgrp_t head;

	union {
		wait_queue_head_t wait;
		struct work_struct work;
	} u;
};

struct z_erofs_vle_unzip_io_sb {
	struct z_erofs_vle_unzip_io io;
	struct super_block *sb;
};

#define Z_EROFS_ONLINEPAGE_COUNT_BITS 2
#define Z_EROFS_ONLINEPAGE_COUNT_MASK ((1 << Z_EROFS_ONLINEPAGE_COUNT_BITS) - 1)
#define Z_EROFS_ONLINEPAGE_INDEX_SHIFT  (Z_EROFS_ONLINEPAGE_COUNT_BITS)

/*
 * waiters (aka. ongoing_packs): # to unlock the page
 * sub-index: 0 - for partial page, >= 1 full page sub-index
 */
typedef atomic_t z_erofs_onlinepage_t;

/* type punning */
union z_erofs_onlinepage_converter {
	z_erofs_onlinepage_t *o;
	unsigned long *v;
};

static inline unsigned z_erofs_onlinepage_index(struct page *page)
{
	union z_erofs_onlinepage_converter u;

	DBG_BUGON(!PagePrivate(page));
	u.v = &page_private(page);

	return atomic_read(u.o) >> Z_EROFS_ONLINEPAGE_INDEX_SHIFT;
}

static inline void z_erofs_onlinepage_init(struct page *page)
{
	union {
		z_erofs_onlinepage_t o;
		unsigned long v;
	/* keep from being unlocked in advance */
	} u = { .o = ATOMIC_INIT(1) };

	set_page_private(page, u.v);
	smp_wmb();
	SetPagePrivate(page);
}

static inline void z_erofs_onlinepage_fixup(struct page *page,
	uintptr_t index, bool down)
{
	unsigned long *p, o, v, id;
repeat:
	p = &page_private(page);
	o = READ_ONCE(*p);

	id = o >> Z_EROFS_ONLINEPAGE_INDEX_SHIFT;
	if (id) {
		if (!index)
			return;

		DBG_BUGON(id != index);
	}

	v = (index << Z_EROFS_ONLINEPAGE_INDEX_SHIFT) |
		((o & Z_EROFS_ONLINEPAGE_COUNT_MASK) + (unsigned)down);
	if (cmpxchg(p, o, v) != o)
		goto repeat;
}

static inline void z_erofs_onlinepage_endio(struct page *page)
{
	union z_erofs_onlinepage_converter u;
	unsigned v;

	DBG_BUGON(!PagePrivate(page));
	u.v = &page_private(page);

	v = atomic_dec_return(u.o);
	if (!(v & Z_EROFS_ONLINEPAGE_COUNT_MASK)) {
		ClearPagePrivate(page);
		if (!PageError(page))
			SetPageUptodate(page);
		unlock_page(page);
	}

	debugln("%s, page %p value %x", __func__, page, atomic_read(u.o));
}

#define Z_EROFS_VLE_VMAP_ONSTACK_PAGES	\
	min_t(unsigned int, THREAD_SIZE / 8 / sizeof(struct page *), 96U)
#define Z_EROFS_VLE_VMAP_GLOBAL_PAGES	2048

#endif

