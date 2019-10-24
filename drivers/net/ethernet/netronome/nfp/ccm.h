/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2016-2019 Netronome Systems, Inc. */

#ifndef NFP_CCM_H
#define NFP_CCM_H 1

#include <linux/bitmap.h>
#include <linux/skbuff.h>
#include <linux/wait.h>

struct nfp_app;
struct nfp_net;

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
	NFP_CCM_TYPE_CRYPTO_RESET	= 9,
	NFP_CCM_TYPE_CRYPTO_ADD		= 10,
	NFP_CCM_TYPE_CRYPTO_DEL		= 11,
	NFP_CCM_TYPE_CRYPTO_UPDATE	= 12,
	__NFP_CCM_TYPE_MAX,
};

#define NFP_CCM_ABI_VERSION		1

#define NFP_CCM_TYPE_REPLY_BIT		7
#define __NFP_CCM_REPLY(req)		(BIT(NFP_CCM_TYPE_REPLY_BIT) | (req))

struct nfp_ccm_hdr {
	union {
		struct {
			u8 type;
			u8 ver;
			__be16 tag;
		};
		__be32 raw;
	};
};

static inline u8 nfp_ccm_get_type(struct sk_buff *skb)
{
	struct nfp_ccm_hdr *hdr;

	hdr = (struct nfp_ccm_hdr *)skb->data;

	return hdr->type;
}

static inline __be16 __nfp_ccm_get_tag(struct sk_buff *skb)
{
	struct nfp_ccm_hdr *hdr;

	hdr = (struct nfp_ccm_hdr *)skb->data;

	return hdr->tag;
}

static inline unsigned int nfp_ccm_get_tag(struct sk_buff *skb)
{
	return be16_to_cpu(__nfp_ccm_get_tag(skb));
}

#define NFP_NET_MBOX_TLV_TYPE		GENMASK(31, 16)
#define NFP_NET_MBOX_TLV_LEN		GENMASK(15, 0)

enum nfp_ccm_mbox_tlv_type {
	NFP_NET_MBOX_TLV_TYPE_UNKNOWN	= 0,
	NFP_NET_MBOX_TLV_TYPE_END	= 1,
	NFP_NET_MBOX_TLV_TYPE_MSG	= 2,
	NFP_NET_MBOX_TLV_TYPE_MSG_NOSUP	= 3,
	NFP_NET_MBOX_TLV_TYPE_RESV	= 4,
};

/* Implementation */

/**
 * struct nfp_ccm - common control message handling
 * @app:		APP handle
 *
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
	wait_queue_head_t wq;
};

int nfp_ccm_init(struct nfp_ccm *ccm, struct nfp_app *app);
void nfp_ccm_clean(struct nfp_ccm *ccm);
void nfp_ccm_rx(struct nfp_ccm *ccm, struct sk_buff *skb);
struct sk_buff *
nfp_ccm_communicate(struct nfp_ccm *ccm, struct sk_buff *skb,
		    enum nfp_ccm_type type, unsigned int reply_size);

int nfp_ccm_mbox_alloc(struct nfp_net *nn);
void nfp_ccm_mbox_free(struct nfp_net *nn);
int nfp_ccm_mbox_init(struct nfp_net *nn);
void nfp_ccm_mbox_clean(struct nfp_net *nn);
bool nfp_ccm_mbox_fits(struct nfp_net *nn, unsigned int size);
struct sk_buff *
nfp_ccm_mbox_msg_alloc(struct nfp_net *nn, unsigned int req_size,
		       unsigned int reply_size, gfp_t flags);
int __nfp_ccm_mbox_communicate(struct nfp_net *nn, struct sk_buff *skb,
			       enum nfp_ccm_type type,
			       unsigned int reply_size,
			       unsigned int max_reply_size, bool critical);
int nfp_ccm_mbox_communicate(struct nfp_net *nn, struct sk_buff *skb,
			     enum nfp_ccm_type type,
			     unsigned int reply_size,
			     unsigned int max_reply_size);
int nfp_ccm_mbox_post(struct nfp_net *nn, struct sk_buff *skb,
		      enum nfp_ccm_type type, unsigned int max_reply_size);
#endif
