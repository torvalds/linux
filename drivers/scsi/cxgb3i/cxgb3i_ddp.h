/*
 * cxgb3i_ddp.h: Chelsio S3xx iSCSI DDP Manager.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#ifndef __CXGB3I_ULP2_DDP_H__
#define __CXGB3I_ULP2_DDP_H__

#include <linux/vmalloc.h>

/**
 * struct cxgb3i_tag_format - cxgb3i ulp tag format for an iscsi entity
 *
 * @sw_bits:	# of bits used by iscsi software layer
 * @rsvd_bits:	# of bits used by h/w
 * @rsvd_shift:	h/w bits shift left
 * @rsvd_mask:	reserved bit mask
 */
struct cxgb3i_tag_format {
	unsigned char sw_bits;
	unsigned char rsvd_bits;
	unsigned char rsvd_shift;
	unsigned char filler[1];
	u32 rsvd_mask;
};

/**
 * struct cxgb3i_gather_list - cxgb3i direct data placement memory
 *
 * @tag:	ddp tag
 * @length:	total data buffer length
 * @offset:	initial offset to the 1st page
 * @nelem:	# of pages
 * @pages:	page pointers
 * @phys_addr:	physical address
 */
struct cxgb3i_gather_list {
	u32 tag;
	unsigned int length;
	unsigned int offset;
	unsigned int nelem;
	struct page **pages;
	dma_addr_t phys_addr[0];
};

/**
 * struct cxgb3i_ddp_info - cxgb3i direct data placement for pdu payload
 *
 * @list:	list head to link elements
 * @tdev:	pointer to t3cdev used by cxgb3 driver
 * @max_txsz:	max tx packet size for ddp
 * @max_rxsz:	max rx packet size for ddp
 * @llimit:	lower bound of the page pod memory
 * @ulimit:	upper bound of the page pod memory
 * @nppods:	# of page pod entries
 * @idx_last:	page pod entry last used
 * @idx_bits:	# of bits the pagepod index would take
 * @idx_mask:	pagepod index mask
 * @rsvd_tag_mask: tag mask
 * @map_lock:	lock to synchonize access to the page pod map
 * @gl_map:	ddp memory gather list
 * @gl_skb:	skb used to program the pagepod
 */
struct cxgb3i_ddp_info {
	struct list_head list;
	struct t3cdev *tdev;
	struct pci_dev *pdev;
	unsigned int max_txsz;
	unsigned int max_rxsz;
	unsigned int llimit;
	unsigned int ulimit;
	unsigned int nppods;
	unsigned int idx_last;
	unsigned char idx_bits;
	unsigned char filler[3];
	u32 idx_mask;
	u32 rsvd_tag_mask;
	spinlock_t map_lock;
	struct cxgb3i_gather_list **gl_map;
	struct sk_buff **gl_skb;
};

#define ISCSI_PDU_NONPAYLOAD_LEN	312 /* bhs(48) + ahs(256) + digest(8) */
#define ULP2_MAX_PKT_SIZE	16224
#define ULP2_MAX_PDU_PAYLOAD	(ULP2_MAX_PKT_SIZE - ISCSI_PDU_NONPAYLOAD_LEN)
#define PPOD_PAGES_MAX		4
#define PPOD_PAGES_SHIFT	2	/* 4 pages per pod */

/*
 * struct pagepod_hdr, pagepod - pagepod format
 */
struct pagepod_hdr {
	u32 vld_tid;
	u32 pgsz_tag_clr;
	u32 maxoffset;
	u32 pgoffset;
	u64 rsvd;
};

struct pagepod {
	struct pagepod_hdr hdr;
	u64 addr[PPOD_PAGES_MAX + 1];
};

#define PPOD_SIZE		sizeof(struct pagepod)	/* 64 */
#define PPOD_SIZE_SHIFT		6

#define PPOD_COLOR_SHIFT	0
#define PPOD_COLOR_SIZE		6
#define PPOD_COLOR_MASK		((1 << PPOD_COLOR_SIZE) - 1)

#define PPOD_IDX_SHIFT		PPOD_COLOR_SIZE
#define PPOD_IDX_MAX_SIZE	24

#define S_PPOD_TID    0
#define M_PPOD_TID    0xFFFFFF
#define V_PPOD_TID(x) ((x) << S_PPOD_TID)

#define S_PPOD_VALID    24
#define V_PPOD_VALID(x) ((x) << S_PPOD_VALID)
#define F_PPOD_VALID    V_PPOD_VALID(1U)

#define S_PPOD_COLOR    0
#define M_PPOD_COLOR    0x3F
#define V_PPOD_COLOR(x) ((x) << S_PPOD_COLOR)

#define S_PPOD_TAG    6
#define M_PPOD_TAG    0xFFFFFF
#define V_PPOD_TAG(x) ((x) << S_PPOD_TAG)

#define S_PPOD_PGSZ    30
#define M_PPOD_PGSZ    0x3
#define V_PPOD_PGSZ(x) ((x) << S_PPOD_PGSZ)

/*
 * large memory chunk allocation/release
 * use vmalloc() if kmalloc() fails
 */
static inline void *cxgb3i_alloc_big_mem(unsigned int size,
					 gfp_t gfp)
{
	void *p = kmalloc(size, gfp);
	if (!p)
		p = vmalloc(size);
	if (p)
		memset(p, 0, size);
	return p;
}

static inline void cxgb3i_free_big_mem(void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}

/*
 * cxgb3i ddp tag are 32 bits, it consists of reserved bits used by h/w and
 * non-reserved bits that can be used by the iscsi s/w.
 * The reserved bits are identified by the rsvd_bits and rsvd_shift fields
 * in struct cxgb3i_tag_format.
 *
 * The upper most reserved bit can be used to check if a tag is ddp tag or not:
 * 	if the bit is 0, the tag is a valid ddp tag
 */

/**
 * cxgb3i_is_ddp_tag - check if a given tag is a hw/ddp tag
 * @tformat: tag format information
 * @tag: tag to be checked
 *
 * return true if the tag is a ddp tag, false otherwise.
 */
static inline int cxgb3i_is_ddp_tag(struct cxgb3i_tag_format *tformat, u32 tag)
{
	return !(tag & (1 << (tformat->rsvd_bits + tformat->rsvd_shift - 1)));
}

/**
 * cxgb3i_sw_tag_usable - check if s/w tag has enough bits left for hw bits
 * @tformat: tag format information
 * @sw_tag: s/w tag to be checked
 *
 * return true if the tag can be used for hw ddp tag, false otherwise.
 */
static inline int cxgb3i_sw_tag_usable(struct cxgb3i_tag_format *tformat,
					u32 sw_tag)
{
	sw_tag >>= (32 - tformat->rsvd_bits);
	return !sw_tag;
}

/**
 * cxgb3i_set_non_ddp_tag - mark a given s/w tag as an invalid ddp tag
 * @tformat: tag format information
 * @sw_tag: s/w tag to be checked
 *
 * insert 1 at the upper most reserved bit to mark it as an invalid ddp tag.
 */
static inline u32 cxgb3i_set_non_ddp_tag(struct cxgb3i_tag_format *tformat,
					 u32 sw_tag)
{
	unsigned char shift = tformat->rsvd_bits + tformat->rsvd_shift - 1;
	u32 mask = (1 << shift) - 1;

	if (sw_tag && (sw_tag & ~mask)) {
		u32 v1 = sw_tag & ((1 << shift) - 1);
		u32 v2 = (sw_tag >> (shift - 1)) << shift;

		return v2 | v1 | 1 << shift;
	}
	return sw_tag | 1 << shift;
}

/**
 * cxgb3i_ddp_tag_base - shift s/w tag bits so that reserved bits are not used
 * @tformat: tag format information
 * @sw_tag: s/w tag to be checked
 */
static inline u32 cxgb3i_ddp_tag_base(struct cxgb3i_tag_format *tformat,
				      u32 sw_tag)
{
	u32 mask = (1 << tformat->rsvd_shift) - 1;

	if (sw_tag && (sw_tag & ~mask)) {
		u32 v1 = sw_tag & mask;
		u32 v2 = sw_tag >> tformat->rsvd_shift;

		v2 <<= tformat->rsvd_shift + tformat->rsvd_bits;
		return v2 | v1;
	}
	return sw_tag;
}

/**
 * cxgb3i_tag_rsvd_bits - get the reserved bits used by the h/w
 * @tformat: tag format information
 * @tag: tag to be checked
 *
 * return the reserved bits in the tag
 */
static inline u32 cxgb3i_tag_rsvd_bits(struct cxgb3i_tag_format *tformat,
				       u32 tag)
{
	if (cxgb3i_is_ddp_tag(tformat, tag))
		return (tag >> tformat->rsvd_shift) & tformat->rsvd_mask;
	return 0;
}

/**
 * cxgb3i_tag_nonrsvd_bits - get the non-reserved bits used by the s/w
 * @tformat: tag format information
 * @tag: tag to be checked
 *
 * return the non-reserved bits in the tag.
 */
static inline u32 cxgb3i_tag_nonrsvd_bits(struct cxgb3i_tag_format *tformat,
					  u32 tag)
{
	unsigned char shift = tformat->rsvd_bits + tformat->rsvd_shift - 1;
	u32 v1, v2;

	if (cxgb3i_is_ddp_tag(tformat, tag)) {
		v1 = tag & ((1 << tformat->rsvd_shift) - 1);
		v2 = (tag >> (shift + 1)) << tformat->rsvd_shift;
	} else {
		u32 mask = (1 << shift) - 1;

		tag &= ~(1 << shift);
		v1 = tag & mask;
		v2 = (tag >> 1) & ~mask;
	}
	return v1 | v2;
}

int cxgb3i_ddp_tag_reserve(struct t3cdev *, unsigned int tid,
			   struct cxgb3i_tag_format *, u32 *tag,
			   struct cxgb3i_gather_list *, gfp_t gfp);
void cxgb3i_ddp_tag_release(struct t3cdev *, u32 tag);

struct cxgb3i_gather_list *cxgb3i_ddp_make_gl(unsigned int xferlen,
				struct scatterlist *sgl,
				unsigned int sgcnt,
				struct pci_dev *pdev,
				gfp_t gfp);
void cxgb3i_ddp_release_gl(struct cxgb3i_gather_list *gl,
				struct pci_dev *pdev);

int cxgb3i_setup_conn_host_pagesize(struct t3cdev *, unsigned int tid,
				    int reply);
int cxgb3i_setup_conn_pagesize(struct t3cdev *, unsigned int tid, int reply,
			       unsigned long pgsz);
int cxgb3i_setup_conn_digest(struct t3cdev *, unsigned int tid,
				int hcrc, int dcrc, int reply);
int cxgb3i_ddp_find_page_index(unsigned long pgsz);
int cxgb3i_adapter_ddp_info(struct t3cdev *, struct cxgb3i_tag_format *,
			    unsigned int *txsz, unsigned int *rxsz);

void cxgb3i_ddp_init(struct t3cdev *);
void cxgb3i_ddp_cleanup(struct t3cdev *);
#endif
