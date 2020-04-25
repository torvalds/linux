/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2020 Chelsio Communications.  All rights reserved. */

#ifndef __CHCR_COMMON_H__
#define __CHCR_COMMON_H__

#include "cxgb4.h"

#define CHCR_MAX_SALT                      4
#define CHCR_KEYCTX_MAC_KEY_SIZE_128       0
#define CHCR_KEYCTX_CIPHER_KEY_SIZE_128    0
#define CHCR_SCMD_CIPHER_MODE_AES_GCM      2
#define CHCR_SCMD_CIPHER_MODE_AES_CTR      3
#define CHCR_CPL_TX_SEC_PDU_LEN_64BIT      2
#define CHCR_SCMD_SEQ_NO_CTRL_64BIT        3
#define CHCR_SCMD_PROTO_VERSION_TLS        0
#define CHCR_SCMD_PROTO_VERSION_GENERIC    4
#define CHCR_SCMD_AUTH_MODE_GHASH          4
#define AES_BLOCK_LEN                      16

enum chcr_state {
	CHCR_INIT = 0,
	CHCR_ATTACH,
	CHCR_DETACH,
};

struct chcr_dev {
	spinlock_t lock_chcr_dev; /* chcr dev structure lock */
	enum chcr_state state;
	atomic_t inflight;
	int wqretry;
	struct delayed_work detach_work;
	struct completion detach_comp;
	unsigned char tx_channel_id;
};

struct uld_ctx {
	struct list_head entry;
	struct cxgb4_lld_info lldi;
	struct chcr_dev dev;
};

struct ktls_key_ctx {
	__be32 ctx_hdr;
	u8 salt[CHCR_MAX_SALT];
	__be64 iv_to_auth;
	unsigned char key[TLS_CIPHER_AES_GCM_128_KEY_SIZE +
			  TLS_CIPHER_AES_GCM_256_TAG_SIZE];
};

/* Crypto key context */
#define KEY_CONTEXT_CTX_LEN_S           24
#define KEY_CONTEXT_CTX_LEN_V(x)        ((x) << KEY_CONTEXT_CTX_LEN_S)

#define KEY_CONTEXT_SALT_PRESENT_S      10
#define KEY_CONTEXT_SALT_PRESENT_V(x)   ((x) << KEY_CONTEXT_SALT_PRESENT_S)
#define KEY_CONTEXT_SALT_PRESENT_F      KEY_CONTEXT_SALT_PRESENT_V(1U)

#define KEY_CONTEXT_VALID_S     0
#define KEY_CONTEXT_VALID_V(x)  ((x) << KEY_CONTEXT_VALID_S)
#define KEY_CONTEXT_VALID_F     KEY_CONTEXT_VALID_V(1U)

#define KEY_CONTEXT_CK_SIZE_S           6
#define KEY_CONTEXT_CK_SIZE_V(x)        ((x) << KEY_CONTEXT_CK_SIZE_S)

#define KEY_CONTEXT_MK_SIZE_S           2
#define KEY_CONTEXT_MK_SIZE_V(x)        ((x) << KEY_CONTEXT_MK_SIZE_S)

#define KEY_CONTEXT_OPAD_PRESENT_S      11
#define KEY_CONTEXT_OPAD_PRESENT_V(x)   ((x) << KEY_CONTEXT_OPAD_PRESENT_S)
#define KEY_CONTEXT_OPAD_PRESENT_F      KEY_CONTEXT_OPAD_PRESENT_V(1U)

#define FILL_KEY_CTX_HDR(ck_size, mk_size, ctx_len) \
		htonl(KEY_CONTEXT_MK_SIZE_V(mk_size) | \
		      KEY_CONTEXT_CK_SIZE_V(ck_size) | \
		      KEY_CONTEXT_VALID_F | \
		      KEY_CONTEXT_SALT_PRESENT_F | \
		      KEY_CONTEXT_CTX_LEN_V((ctx_len)))

struct uld_ctx *assign_chcr_device(void);

static inline void *chcr_copy_to_txd(const void *src, const struct sge_txq *q,
				     void *pos, int length)
{
	int left = (void *)q->stat - pos;
	u64 *p;

	if (likely(length <= left)) {
		memcpy(pos, src, length);
		pos += length;
	} else {
		memcpy(pos, src, left);
		memcpy(q->desc, src + left, length - left);
		pos = (void *)q->desc + (length - left);
	}
	/* 0-pad to multiple of 16 */
	p = PTR_ALIGN(pos, 8);
	if ((uintptr_t)p & 8) {
		*p = 0;
		return p + 1;
	}
	return p;
}

static inline unsigned int chcr_txq_avail(const struct sge_txq *q)
{
	return q->size - 1 - q->in_use;
}

static inline void chcr_txq_advance(struct sge_txq *q, unsigned int n)
{
	q->in_use += n;
	q->pidx += n;
	if (q->pidx >= q->size)
		q->pidx -= q->size;
}

static inline void chcr_eth_txq_stop(struct sge_eth_txq *q)
{
	netif_tx_stop_queue(q->txq);
	q->q.stops++;
}

static inline unsigned int chcr_sgl_len(unsigned int n)
{
	n--;
	return (3 * n) / 2 + (n & 1) + 2;
}

static inline unsigned int chcr_flits_to_desc(unsigned int n)
{
	WARN_ON(n > SGE_MAX_WR_LEN / 8);
	return DIV_ROUND_UP(n, 8);
}
#endif /* __CHCR_COMMON_H__ */
