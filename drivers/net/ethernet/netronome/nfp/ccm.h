/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2016-2019 Netronome Systems, Inc. */

#ifndef NFP_CCM_H
#define NFP_CCM_H 1

#include <linux/bitmap.h>
#include <linux/skbuff.h>
#include <linux/wait.h>

struct nfp_app;

/* Firmware ABI */

enum nfp_ccm_type {
	NFP_CCM_TYPE_BPF_MAP_ALLOC	= 1,
	NFP_CCM_TYPE_BPF_MAP_FREE	= 2,
	NFP_CCM_TYPE_BPF_MAP_LOOKUP	= 3,
	NFP_CCM_TYPE_BPF_MAP_UPDATE	= 4,
	NFP_CCM_TYPE_BPF_MAP_DELETE	= 5,
	NFP_CCM_TYPE_BPF_MAP_GETNEXT	= 6,
	NFP_CCM_TYPE_BPF_MAP_GETFIRST	= 7,
	NFP_CCM_TYPE_BPF_BPF_EVENT	= 8,
	__NFP_CCM_TYPE_MAX,
};

#define NFP_CCM_ABI_VERSION		1

struct nfp_ccm_hdr {
	u8 type;
	u8 ver;
	__be16 tag;
};

static inline u8 nfp_ccm_get_type(struct sk_buff *skb)
{
	struct nfp_ccm_hdr *hdr;

	hdr = (struct nfp_ccm_hdr *)skb->data;

	return hdr->type;
}

static inline unsigned int nfp_ccm_get_tag(struct sk_buff *skb)
{
	struct nfp_ccm_hdr *hdr;

	hdr = (struct nfp_ccm_hdr *)skb->data;

	return be16_to_cpu(hdr->tag);
}

/* Implementation */

/**
 * struct nfp_ccm - common control message handling
 * @tag_allocator:	bitmap of control message tags in use
 * @tag_alloc_next:	next tag bit to allocate
 * @tag_alloc_last:	next tag bit to be freed
 *
 * @replies:		received cmsg replies waiting to be consumed
 * @wq:			work queue for waiting for cmsg replies
 */
struct nfp_ccm {
	struct nfp_app *app;

	DECLARE_BITMAP(tag_allocator, U16_MAX + 1);
	u16 tag_alloc_next;
	u16 tag_alloc_last;

	struct sk_buff_head replies;
	struct wait_queue_head wq;
};

int nfp_ccm_init(struct nfp_ccm *ccm, struct nfp_app *app);
void nfp_ccm_clean(struct nfp_ccm *ccm);
void nfp_ccm_rx(struct nfp_ccm *ccm, struct sk_buff *skb);
struct sk_buff *
nfp_ccm_communicate(struct nfp_ccm *ccm, struct sk_buff *skb,
		    enum nfp_ccm_type type, unsigned int reply_size);
#endif
