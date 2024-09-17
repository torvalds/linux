/*
 * libcxgb_ppm.h: Chelsio common library for T3/T4/T5 iSCSI ddp operation
 *
 * Copyright (c) 2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#ifndef	__LIBCXGB_PPM_H__
#define	__LIBCXGB_PPM_H__

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/bitmap.h>

struct cxgbi_pagepod_hdr {
	u32 vld_tid;
	u32 pgsz_tag_clr;
	u32 max_offset;
	u32 page_offset;
	u64 rsvd;
};

#define PPOD_PAGES_MAX			4
struct cxgbi_pagepod {
	struct cxgbi_pagepod_hdr hdr;
	__be64 addr[PPOD_PAGES_MAX + 1];
};

/* ddp tag format
 * for a 32-bit tag:
 * bit #
 * 31 .....   .....  0
 *     X   Y...Y Z...Z, where
 *     ^   ^^^^^ ^^^^
 *     |   |      |____ when ddp bit = 0: color bits
 *     |   |
 *     |   |____ when ddp bit = 0: idx into the ddp memory region
 *     |
 *     |____ ddp bit: 0 - ddp tag, 1 - non-ddp tag
 *
 *  [page selector:2] [sw/free bits] [0] [idx] [color:6]
 */

#define DDP_PGIDX_MAX		4
#define DDP_PGSZ_BASE_SHIFT	12	/* base page 4K */

struct cxgbi_task_tag_info {
	unsigned char flags;
#define CXGBI_PPOD_INFO_FLAG_VALID	0x1
#define CXGBI_PPOD_INFO_FLAG_MAPPED	0x2
	unsigned char cid;
	unsigned short pg_shift;
	unsigned int npods;
	unsigned int idx;
	unsigned int tag;
	struct cxgbi_pagepod_hdr hdr;
	int nents;
	int nr_pages;
	struct scatterlist *sgl;
};

struct cxgbi_tag_format {
	unsigned char pgsz_order[DDP_PGIDX_MAX];
	unsigned char pgsz_idx_dflt;
	unsigned char free_bits:4;
	unsigned char color_bits:4;
	unsigned char idx_bits;
	unsigned char rsvd_bits;
	unsigned int  no_ddp_mask;
	unsigned int  idx_mask;
	unsigned int  color_mask;
	unsigned int  idx_clr_mask;
	unsigned int  rsvd_mask;
};

struct cxgbi_ppod_data {
	unsigned char pg_idx:2;
	unsigned char color:6;
	unsigned char chan_id;
	unsigned short npods;
	unsigned long caller_data;
};

/* per cpu ppm pool */
struct cxgbi_ppm_pool {
	unsigned int base;		/* base index */
	unsigned int next;		/* next possible free index */
	spinlock_t lock;		/* ppm pool lock */
	unsigned long bmap[];
} ____cacheline_aligned_in_smp;

struct cxgbi_ppm {
	struct kref refcnt;
	struct net_device *ndev;	/* net_device, 1st port */
	struct pci_dev *pdev;
	void *lldev;
	void **ppm_pp;
	struct cxgbi_tag_format tformat;
	unsigned int ppmax;
	unsigned int llimit;
	unsigned int base_idx;

	unsigned int pool_rsvd;
	unsigned int pool_index_max;
	struct cxgbi_ppm_pool __percpu *pool;
	/* map lock */
	spinlock_t map_lock;		/* ppm map lock */
	unsigned int bmap_index_max;
	unsigned int next;
	unsigned int max_index_in_edram;
	unsigned long *ppod_bmap;
	struct cxgbi_ppod_data ppod_data[];
};

#define DDP_THRESHOLD		512

#define PPOD_PAGES_SHIFT	2       /*  4 pages per pod */

#define IPPOD_SIZE               sizeof(struct cxgbi_pagepod)  /*  64 */
#define PPOD_SIZE_SHIFT         6

/* page pods are allocated in groups of this size (must be power of 2) */
#define PPOD_CLUSTER_SIZE	16U

#define ULPMEM_DSGL_MAX_NPPODS	16	/*  1024/PPOD_SIZE */
#define ULPMEM_IDATA_MAX_NPPODS	3	/* (PPOD_SIZE * 3 + ulptx hdr) < 256B */
#define PCIE_MEMWIN_MAX_NPPODS	16	/*  1024/PPOD_SIZE */

#define PPOD_COLOR_SHIFT	0
#define PPOD_COLOR(x)		((x) << PPOD_COLOR_SHIFT)

#define PPOD_IDX_SHIFT          6
#define PPOD_IDX_MAX_SIZE       24

#define PPOD_TID_SHIFT		0
#define PPOD_TID(x)		((x) << PPOD_TID_SHIFT)

#define PPOD_TAG_SHIFT		6
#define PPOD_TAG(x)		((x) << PPOD_TAG_SHIFT)

#define PPOD_VALID_SHIFT	24
#define PPOD_VALID(x)		((x) << PPOD_VALID_SHIFT)
#define PPOD_VALID_FLAG		PPOD_VALID(1U)

#define PPOD_PI_EXTRACT_CTL_SHIFT	31
#define PPOD_PI_EXTRACT_CTL(x)		((x) << PPOD_PI_EXTRACT_CTL_SHIFT)
#define PPOD_PI_EXTRACT_CTL_FLAG	V_PPOD_PI_EXTRACT_CTL(1U)

#define PPOD_PI_TYPE_SHIFT		29
#define PPOD_PI_TYPE_MASK		0x3
#define PPOD_PI_TYPE(x)			((x) << PPOD_PI_TYPE_SHIFT)

#define PPOD_PI_CHECK_CTL_SHIFT		27
#define PPOD_PI_CHECK_CTL_MASK		0x3
#define PPOD_PI_CHECK_CTL(x)		((x) << PPOD_PI_CHECK_CTL_SHIFT)

#define PPOD_PI_REPORT_CTL_SHIFT	25
#define PPOD_PI_REPORT_CTL_MASK		0x3
#define PPOD_PI_REPORT_CTL(x)		((x) << PPOD_PI_REPORT_CTL_SHIFT)

static inline int cxgbi_ppm_is_ddp_tag(struct cxgbi_ppm *ppm, u32 tag)
{
	return !(tag & ppm->tformat.no_ddp_mask);
}

static inline int cxgbi_ppm_sw_tag_is_usable(struct cxgbi_ppm *ppm,
					     u32 tag)
{
	/* the sw tag must be using <= 31 bits */
	return !(tag & 0x80000000U);
}

static inline int cxgbi_ppm_make_non_ddp_tag(struct cxgbi_ppm *ppm,
					     u32 sw_tag,
					     u32 *final_tag)
{
	struct cxgbi_tag_format *tformat = &ppm->tformat;

	if (!cxgbi_ppm_sw_tag_is_usable(ppm, sw_tag)) {
		pr_info("sw_tag 0x%x NOT usable.\n", sw_tag);
		return -EINVAL;
	}

	if (!sw_tag) {
		*final_tag = tformat->no_ddp_mask;
	} else {
		unsigned int shift = tformat->idx_bits + tformat->color_bits;
		u32 lower = sw_tag & tformat->idx_clr_mask;
		u32 upper = (sw_tag >> shift) << (shift + 1);

		*final_tag = upper | tformat->no_ddp_mask | lower;
	}
	return 0;
}

static inline u32 cxgbi_ppm_decode_non_ddp_tag(struct cxgbi_ppm *ppm,
					       u32 tag)
{
	struct cxgbi_tag_format *tformat = &ppm->tformat;
	unsigned int shift = tformat->idx_bits + tformat->color_bits;
	u32 lower = tag & tformat->idx_clr_mask;
	u32 upper = (tag >> tformat->rsvd_bits) << shift;

	return upper | lower;
}

static inline u32 cxgbi_ppm_ddp_tag_get_idx(struct cxgbi_ppm *ppm,
					    u32 ddp_tag)
{
	u32 hw_idx = (ddp_tag >> PPOD_IDX_SHIFT) &
			ppm->tformat.idx_mask;

	return hw_idx - ppm->base_idx;
}

static inline u32 cxgbi_ppm_make_ddp_tag(unsigned int hw_idx,
					 unsigned char color)
{
	return (hw_idx << PPOD_IDX_SHIFT) | ((u32)color);
}

static inline unsigned long
cxgbi_ppm_get_tag_caller_data(struct cxgbi_ppm *ppm,
			      u32 ddp_tag)
{
	u32 idx = cxgbi_ppm_ddp_tag_get_idx(ppm, ddp_tag);

	return ppm->ppod_data[idx].caller_data;
}

/* sw bits are the free bits */
static inline int cxgbi_ppm_ddp_tag_update_sw_bits(struct cxgbi_ppm *ppm,
						   u32 val, u32 orig_tag,
						   u32 *final_tag)
{
	struct cxgbi_tag_format *tformat = &ppm->tformat;
	u32 v = val >> tformat->free_bits;

	if (v) {
		pr_info("sw_bits 0x%x too large, avail bits %u.\n",
			val, tformat->free_bits);
		return -EINVAL;
	}
	if (!cxgbi_ppm_is_ddp_tag(ppm, orig_tag))
		return -EINVAL;

	*final_tag = (val << tformat->rsvd_bits) |
		     (orig_tag & ppm->tformat.rsvd_mask);
	return 0;
}

static inline void cxgbi_ppm_ppod_clear(struct cxgbi_pagepod *ppod)
{
	ppod->hdr.vld_tid = 0U;
}

static inline void cxgbi_tagmask_check(unsigned int tagmask,
				       struct cxgbi_tag_format *tformat)
{
	unsigned int bits = fls(tagmask);

	/* reserve top most 2 bits for page selector */
	tformat->free_bits = 32 - 2 - bits;
	tformat->rsvd_bits = bits;
	tformat->color_bits = PPOD_IDX_SHIFT;
	tformat->idx_bits = bits - 1 - PPOD_IDX_SHIFT;
	tformat->no_ddp_mask = 1 << (bits - 1);
	tformat->idx_mask = (1 << tformat->idx_bits) - 1;
	tformat->color_mask = (1 << PPOD_IDX_SHIFT) - 1;
	tformat->idx_clr_mask = (1 << (bits - 1)) - 1;
	tformat->rsvd_mask = (1 << bits) - 1;

	pr_info("ippm: tagmask 0x%x, rsvd %u=%u+%u+1, mask 0x%x,0x%x, "
		"pg %u,%u,%u,%u.\n",
		tagmask, tformat->rsvd_bits, tformat->idx_bits,
		tformat->color_bits, tformat->no_ddp_mask, tformat->rsvd_mask,
		tformat->pgsz_order[0], tformat->pgsz_order[1],
		tformat->pgsz_order[2], tformat->pgsz_order[3]);
}

int cxgbi_ppm_find_page_index(struct cxgbi_ppm *ppm, unsigned long pgsz);
void cxgbi_ppm_make_ppod_hdr(struct cxgbi_ppm *ppm, u32 tag,
			     unsigned int tid, unsigned int offset,
			     unsigned int length,
			     struct cxgbi_pagepod_hdr *hdr);
void cxgbi_ppm_ppod_release(struct cxgbi_ppm *, u32 idx);
int cxgbi_ppm_ppods_reserve(struct cxgbi_ppm *, unsigned short nr_pages,
			    u32 per_tag_pg_idx, u32 *ppod_idx, u32 *ddp_tag,
			    unsigned long caller_data);
int cxgbi_ppm_init(void **ppm_pp, struct net_device *, struct pci_dev *,
		   void *lldev, struct cxgbi_tag_format *,
		   unsigned int iscsi_size, unsigned int llimit,
		   unsigned int start, unsigned int reserve_factor,
		   unsigned int edram_start, unsigned int edram_size);
int cxgbi_ppm_release(struct cxgbi_ppm *ppm);
void cxgbi_tagmask_check(unsigned int tagmask, struct cxgbi_tag_format *);
unsigned int cxgbi_tagmask_set(unsigned int ppmax);

#endif	/*__LIBCXGB_PPM_H__*/
