/*
 * This file is part of the Chelsio T6 Crypto driver for Linux.
 *
 * Copyright (c) 2003-2016 Chelsio Communications, Inc. All rights reserved.
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
 */

#ifndef __CHCR_CORE_H__
#define __CHCR_CORE_H__

#include <crypto/algapi.h>
#include "t4_hw.h"
#include "cxgb4.h"
#include "t4_msg.h"
#include "cxgb4_uld.h"

#define DRV_MODULE_NAME "chcr"
#define DRV_VERSION "1.0.0.0"

#define MAX_PENDING_REQ_TO_HW 20
#define CHCR_TEST_RESPONSE_TIMEOUT 1000

#define PAD_ERROR_BIT		1
#define CHK_PAD_ERR_BIT(x)	(((x) >> PAD_ERROR_BIT) & 1)

#define MAC_ERROR_BIT		0
#define CHK_MAC_ERR_BIT(x)	(((x) >> MAC_ERROR_BIT) & 1)
#define MAX_SALT                4
#define CIP_WR_MIN_LEN (sizeof(struct chcr_wr) + \
		    sizeof(struct cpl_rx_phys_dsgl) + \
		    sizeof(struct ulptx_sgl))

#define HASH_WR_MIN_LEN (sizeof(struct chcr_wr) + \
			DUMMY_BYTES + \
		    sizeof(struct ulptx_sgl))

#define padap(dev) pci_get_drvdata(dev->u_ctx->lldi.pdev)

struct uld_ctx;

struct _key_ctx {
	__be32 ctx_hdr;
	u8 salt[MAX_SALT];
	__be64 iv_to_auth;
	unsigned char key[0];
};

#define KEYCTX_TX_WR_IV_S  55
#define KEYCTX_TX_WR_IV_M  0x1ffULL
#define KEYCTX_TX_WR_IV_V(x) ((x) << KEYCTX_TX_WR_IV_S)
#define KEYCTX_TX_WR_IV_G(x) \
	(((x) >> KEYCTX_TX_WR_IV_S) & KEYCTX_TX_WR_IV_M)

#define KEYCTX_TX_WR_AAD_S 47
#define KEYCTX_TX_WR_AAD_M 0xffULL
#define KEYCTX_TX_WR_AAD_V(x) ((x) << KEYCTX_TX_WR_AAD_S)
#define KEYCTX_TX_WR_AAD_G(x) (((x) >> KEYCTX_TX_WR_AAD_S) & \
				KEYCTX_TX_WR_AAD_M)

#define KEYCTX_TX_WR_AADST_S 39
#define KEYCTX_TX_WR_AADST_M 0xffULL
#define KEYCTX_TX_WR_AADST_V(x) ((x) << KEYCTX_TX_WR_AADST_S)
#define KEYCTX_TX_WR_AADST_G(x) \
	(((x) >> KEYCTX_TX_WR_AADST_S) & KEYCTX_TX_WR_AADST_M)

#define KEYCTX_TX_WR_CIPHER_S 30
#define KEYCTX_TX_WR_CIPHER_M 0x1ffULL
#define KEYCTX_TX_WR_CIPHER_V(x) ((x) << KEYCTX_TX_WR_CIPHER_S)
#define KEYCTX_TX_WR_CIPHER_G(x) \
	(((x) >> KEYCTX_TX_WR_CIPHER_S) & KEYCTX_TX_WR_CIPHER_M)

#define KEYCTX_TX_WR_CIPHERST_S 23
#define KEYCTX_TX_WR_CIPHERST_M 0x7f
#define KEYCTX_TX_WR_CIPHERST_V(x) ((x) << KEYCTX_TX_WR_CIPHERST_S)
#define KEYCTX_TX_WR_CIPHERST_G(x) \
	(((x) >> KEYCTX_TX_WR_CIPHERST_S) & KEYCTX_TX_WR_CIPHERST_M)

#define KEYCTX_TX_WR_AUTH_S 14
#define KEYCTX_TX_WR_AUTH_M 0x1ff
#define KEYCTX_TX_WR_AUTH_V(x) ((x) << KEYCTX_TX_WR_AUTH_S)
#define KEYCTX_TX_WR_AUTH_G(x) \
	(((x) >> KEYCTX_TX_WR_AUTH_S) & KEYCTX_TX_WR_AUTH_M)

#define KEYCTX_TX_WR_AUTHST_S 7
#define KEYCTX_TX_WR_AUTHST_M 0x7f
#define KEYCTX_TX_WR_AUTHST_V(x) ((x) << KEYCTX_TX_WR_AUTHST_S)
#define KEYCTX_TX_WR_AUTHST_G(x) \
	(((x) >> KEYCTX_TX_WR_AUTHST_S) & KEYCTX_TX_WR_AUTHST_M)

#define KEYCTX_TX_WR_AUTHIN_S 0
#define KEYCTX_TX_WR_AUTHIN_M 0x7f
#define KEYCTX_TX_WR_AUTHIN_V(x) ((x) << KEYCTX_TX_WR_AUTHIN_S)
#define KEYCTX_TX_WR_AUTHIN_G(x) \
	(((x) >> KEYCTX_TX_WR_AUTHIN_S) & KEYCTX_TX_WR_AUTHIN_M)

struct chcr_wr {
	struct fw_crypto_lookaside_wr wreq;
	struct ulp_txpkt ulptx;
	struct ulptx_idata sc_imm;
	struct cpl_tx_sec_pdu sec_cpl;
	struct _key_ctx key_ctx;
};

struct chcr_dev {
	spinlock_t lock_chcr_dev;
	struct uld_ctx *u_ctx;
	unsigned char tx_channel_id;
	unsigned char rx_channel_id;
};

struct uld_ctx {
	struct list_head entry;
	struct cxgb4_lld_info lldi;
	struct chcr_dev *dev;
};

struct sge_opaque_hdr {
	void *dev;
	dma_addr_t addr[MAX_SKB_FRAGS + 1];
};

struct chcr_ipsec_req {
	struct ulp_txpkt ulptx;
	struct ulptx_idata sc_imm;
	struct cpl_tx_sec_pdu sec_cpl;
	struct _key_ctx key_ctx;
};

struct chcr_ipsec_wr {
	struct fw_ulptx_wr wreq;
	struct chcr_ipsec_req req;
};

struct ipsec_sa_entry {
	int hmac_ctrl;
	unsigned int enckey_len;
	unsigned int kctx_len;
	unsigned int authsize;
	__be32 key_ctx_hdr;
	char salt[MAX_SALT];
	char key[2 * AES_MAX_KEY_SIZE];
};

/*
 *      sgl_len - calculates the size of an SGL of the given capacity
 *      @n: the number of SGL entries
 *      Calculates the number of flits needed for a scatter/gather list that
 *      can hold the given number of entries.
 */
static inline unsigned int sgl_len(unsigned int n)
{
	n--;
	return (3 * n) / 2 + (n & 1) + 2;
}

struct uld_ctx *assign_chcr_device(void);
int chcr_send_wr(struct sk_buff *skb);
int start_crypto(void);
int stop_crypto(void);
int chcr_uld_rx_handler(void *handle, const __be64 *rsp,
			const struct pkt_gl *pgl);
int chcr_uld_tx_handler(struct sk_buff *skb, struct net_device *dev);
int chcr_handle_resp(struct crypto_async_request *req, unsigned char *input,
		     int err);
int chcr_ipsec_xmit(struct sk_buff *skb, struct net_device *dev);
void chcr_add_xfrmops(const struct cxgb4_lld_info *lld);
#endif /* __CHCR_CORE_H__ */
