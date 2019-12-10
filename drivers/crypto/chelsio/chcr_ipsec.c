/*
 * This file is part of the Chelsio T6 Crypto driver for Linux.
 *
 * Copyright (c) 2003-2017 Chelsio Communications, Inc. All rights reserved.
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
 * Written and Maintained by:
 *	Atul Gupta (atul.gupta@chelsio.com)
 */

#define pr_fmt(fmt) "chcr:" fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/cryptohash.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/highmem.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <net/esp.h>
#include <net/xfrm.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <crypto/authenc.h>
#include <crypto/internal/aead.h>
#include <crypto/null.h>
#include <crypto/internal/skcipher.h>
#include <crypto/aead.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/hash.h>

#include "chcr_core.h"
#include "chcr_algo.h"
#include "chcr_crypto.h"

/*
 * Max Tx descriptor space we allow for an Ethernet packet to be inlined
 * into a WR.
 */
#define MAX_IMM_TX_PKT_LEN 256
#define GCM_ESP_IV_SIZE     8

static int chcr_xfrm_add_state(struct xfrm_state *x);
static void chcr_xfrm_del_state(struct xfrm_state *x);
static void chcr_xfrm_free_state(struct xfrm_state *x);
static bool chcr_ipsec_offload_ok(struct sk_buff *skb, struct xfrm_state *x);
static void chcr_advance_esn_state(struct xfrm_state *x);

static const struct xfrmdev_ops chcr_xfrmdev_ops = {
	.xdo_dev_state_add      = chcr_xfrm_add_state,
	.xdo_dev_state_delete   = chcr_xfrm_del_state,
	.xdo_dev_state_free     = chcr_xfrm_free_state,
	.xdo_dev_offload_ok     = chcr_ipsec_offload_ok,
	.xdo_dev_state_advance_esn = chcr_advance_esn_state,
};

/* Add offload xfrms to Chelsio Interface */
void chcr_add_xfrmops(const struct cxgb4_lld_info *lld)
{
	struct net_device *netdev = NULL;
	int i;

	for (i = 0; i < lld->nports; i++) {
		netdev = lld->ports[i];
		if (!netdev)
			continue;
		netdev->xfrmdev_ops = &chcr_xfrmdev_ops;
		netdev->hw_enc_features |= NETIF_F_HW_ESP;
		netdev->features |= NETIF_F_HW_ESP;
		rtnl_lock();
		netdev_change_features(netdev);
		rtnl_unlock();
	}
}

static inline int chcr_ipsec_setauthsize(struct xfrm_state *x,
					 struct ipsec_sa_entry *sa_entry)
{
	int hmac_ctrl;
	int authsize = x->aead->alg_icv_len / 8;

	sa_entry->authsize = authsize;

	switch (authsize) {
	case ICV_8:
		hmac_ctrl = CHCR_SCMD_HMAC_CTRL_DIV2;
		break;
	case ICV_12:
		hmac_ctrl = CHCR_SCMD_HMAC_CTRL_IPSEC_96BIT;
		break;
	case ICV_16:
		hmac_ctrl = CHCR_SCMD_HMAC_CTRL_NO_TRUNC;
		break;
	default:
		return -EINVAL;
	}
	return hmac_ctrl;
}

static inline int chcr_ipsec_setkey(struct xfrm_state *x,
				    struct ipsec_sa_entry *sa_entry)
{
	int keylen = (x->aead->alg_key_len + 7) / 8;
	unsigned char *key = x->aead->alg_key;
	int ck_size, key_ctx_size = 0;
	unsigned char ghash_h[AEAD_H_SIZE];
	struct crypto_aes_ctx aes;
	int ret = 0;

	if (keylen > 3) {
		keylen -= 4;  /* nonce/salt is present in the last 4 bytes */
		memcpy(sa_entry->salt, key + keylen, 4);
	}

	if (keylen == AES_KEYSIZE_128) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
	} else if (keylen == AES_KEYSIZE_192) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
	} else if (keylen == AES_KEYSIZE_256) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
	} else {
		pr_err("GCM: Invalid key length %d\n", keylen);
		ret = -EINVAL;
		goto out;
	}

	memcpy(sa_entry->key, key, keylen);
	sa_entry->enckey_len = keylen;
	key_ctx_size = sizeof(struct _key_ctx) +
			      ((DIV_ROUND_UP(keylen, 16)) << 4) +
			      AEAD_H_SIZE;

	sa_entry->key_ctx_hdr = FILL_KEY_CTX_HDR(ck_size,
						 CHCR_KEYCTX_MAC_KEY_SIZE_128,
						 0, 0,
						 key_ctx_size >> 4);

	/* Calculate the H = CIPH(K, 0 repeated 16 times).
	 * It will go in key context
	 */
	ret = aes_expandkey(&aes, key, keylen);
	if (ret) {
		sa_entry->enckey_len = 0;
		goto out;
	}
	memset(ghash_h, 0, AEAD_H_SIZE);
	aes_encrypt(&aes, ghash_h, ghash_h);
	memzero_explicit(&aes, sizeof(aes));

	memcpy(sa_entry->key + (DIV_ROUND_UP(sa_entry->enckey_len, 16) *
	       16), ghash_h, AEAD_H_SIZE);
	sa_entry->kctx_len = ((DIV_ROUND_UP(sa_entry->enckey_len, 16)) << 4) +
			      AEAD_H_SIZE;
out:
	return ret;
}

/*
 * chcr_xfrm_add_state
 * returns 0 on success, negative error if failed to send message to FPGA
 * positive error if FPGA returned a bad response
 */
static int chcr_xfrm_add_state(struct xfrm_state *x)
{
	struct ipsec_sa_entry *sa_entry;
	int res = 0;

	if (x->props.aalgo != SADB_AALG_NONE) {
		pr_debug("CHCR: Cannot offload authenticated xfrm states\n");
		return -EINVAL;
	}
	if (x->props.calgo != SADB_X_CALG_NONE) {
		pr_debug("CHCR: Cannot offload compressed xfrm states\n");
		return -EINVAL;
	}
	if (x->props.family != AF_INET &&
	    x->props.family != AF_INET6) {
		pr_debug("CHCR: Only IPv4/6 xfrm state offloaded\n");
		return -EINVAL;
	}
	if (x->props.mode != XFRM_MODE_TRANSPORT &&
	    x->props.mode != XFRM_MODE_TUNNEL) {
		pr_debug("CHCR: Only transport and tunnel xfrm offload\n");
		return -EINVAL;
	}
	if (x->id.proto != IPPROTO_ESP) {
		pr_debug("CHCR: Only ESP xfrm state offloaded\n");
		return -EINVAL;
	}
	if (x->encap) {
		pr_debug("CHCR: Encapsulated xfrm state not offloaded\n");
		return -EINVAL;
	}
	if (!x->aead) {
		pr_debug("CHCR: Cannot offload xfrm states without aead\n");
		return -EINVAL;
	}
	if (x->aead->alg_icv_len != 128 &&
	    x->aead->alg_icv_len != 96) {
		pr_debug("CHCR: Cannot offload xfrm states with AEAD ICV length other than 96b & 128b\n");
	return -EINVAL;
	}
	if ((x->aead->alg_key_len != 128 + 32) &&
	    (x->aead->alg_key_len != 256 + 32)) {
		pr_debug("CHCR: Cannot offload xfrm states with AEAD key length other than 128/256 bit\n");
		return -EINVAL;
	}
	if (x->tfcpad) {
		pr_debug("CHCR: Cannot offload xfrm states with tfc padding\n");
		return -EINVAL;
	}
	if (!x->geniv) {
		pr_debug("CHCR: Cannot offload xfrm states without geniv\n");
		return -EINVAL;
	}
	if (strcmp(x->geniv, "seqiv")) {
		pr_debug("CHCR: Cannot offload xfrm states with geniv other than seqiv\n");
		return -EINVAL;
	}

	sa_entry = kzalloc(sizeof(*sa_entry), GFP_KERNEL);
	if (!sa_entry) {
		res = -ENOMEM;
		goto out;
	}

	sa_entry->hmac_ctrl = chcr_ipsec_setauthsize(x, sa_entry);
	if (x->props.flags & XFRM_STATE_ESN)
		sa_entry->esn = 1;
	chcr_ipsec_setkey(x, sa_entry);
	x->xso.offload_handle = (unsigned long)sa_entry;
	try_module_get(THIS_MODULE);
out:
	return res;
}

static void chcr_xfrm_del_state(struct xfrm_state *x)
{
	/* do nothing */
	if (!x->xso.offload_handle)
		return;
}

static void chcr_xfrm_free_state(struct xfrm_state *x)
{
	struct ipsec_sa_entry *sa_entry;

	if (!x->xso.offload_handle)
		return;

	sa_entry = (struct ipsec_sa_entry *)x->xso.offload_handle;
	kfree(sa_entry);
	module_put(THIS_MODULE);
}

static bool chcr_ipsec_offload_ok(struct sk_buff *skb, struct xfrm_state *x)
{
	if (x->props.family == AF_INET) {
		/* Offload with IP options is not supported yet */
		if (ip_hdr(skb)->ihl > 5)
			return false;
	} else {
		/* Offload with IPv6 extension headers is not support yet */
		if (ipv6_ext_hdr(ipv6_hdr(skb)->nexthdr))
			return false;
	}
	/* Inline single pdu */
	if (skb_shinfo(skb)->gso_size)
		return false;
	return true;
}

static void chcr_advance_esn_state(struct xfrm_state *x)
{
	/* do nothing */
	if (!x->xso.offload_handle)
		return;
}

static inline int is_eth_imm(const struct sk_buff *skb,
			     struct ipsec_sa_entry *sa_entry)
{
	unsigned int kctx_len;
	int hdrlen;

	kctx_len = sa_entry->kctx_len;
	hdrlen = sizeof(struct fw_ulptx_wr) +
		 sizeof(struct chcr_ipsec_req) + kctx_len;

	hdrlen += sizeof(struct cpl_tx_pkt);
	if (sa_entry->esn)
		hdrlen += (DIV_ROUND_UP(sizeof(struct chcr_ipsec_aadiv), 16)
			   << 4);
	if (skb->len <= MAX_IMM_TX_PKT_LEN - hdrlen)
		return hdrlen;
	return 0;
}

static inline unsigned int calc_tx_sec_flits(const struct sk_buff *skb,
					     struct ipsec_sa_entry *sa_entry,
					     bool *immediate)
{
	unsigned int kctx_len;
	unsigned int flits;
	int aadivlen;
	int hdrlen;

	kctx_len = sa_entry->kctx_len;
	hdrlen = is_eth_imm(skb, sa_entry);
	aadivlen = sa_entry->esn ? DIV_ROUND_UP(sizeof(struct chcr_ipsec_aadiv),
						16) : 0;
	aadivlen <<= 4;

	/* If the skb is small enough, we can pump it out as a work request
	 * with only immediate data.  In that case we just have to have the
	 * TX Packet header plus the skb data in the Work Request.
	 */

	if (hdrlen) {
		*immediate = true;
		return DIV_ROUND_UP(skb->len + hdrlen, sizeof(__be64));
	}

	flits = sgl_len(skb_shinfo(skb)->nr_frags + 1);

	/* Otherwise, we're going to have to construct a Scatter gather list
	 * of the skb body and fragments.  We also include the flits necessary
	 * for the TX Packet Work Request and CPL.  We always have a firmware
	 * Write Header (incorporated as part of the cpl_tx_pkt_lso and
	 * cpl_tx_pkt structures), followed by either a TX Packet Write CPL
	 * message or, if we're doing a Large Send Offload, an LSO CPL message
	 * with an embedded TX Packet Write CPL message.
	 */
	flits += (sizeof(struct fw_ulptx_wr) +
		  sizeof(struct chcr_ipsec_req) +
		  kctx_len +
		  sizeof(struct cpl_tx_pkt_core) +
		  aadivlen) / sizeof(__be64);
	return flits;
}

inline void *copy_esn_pktxt(struct sk_buff *skb,
			    struct net_device *dev,
			    void *pos,
			    struct ipsec_sa_entry *sa_entry)
{
	struct chcr_ipsec_aadiv *aadiv;
	struct ulptx_idata *sc_imm;
	struct ip_esp_hdr *esphdr;
	struct xfrm_offload *xo;
	struct sge_eth_txq *q;
	struct adapter *adap;
	struct port_info *pi;
	__be64 seqno;
	u32 qidx;
	u32 seqlo;
	u8 *iv;
	int eoq;
	int len;

	pi = netdev_priv(dev);
	adap = pi->adapter;
	qidx = skb->queue_mapping;
	q = &adap->sge.ethtxq[qidx + pi->first_qset];

	/* end of queue, reset pos to start of queue */
	eoq = (void *)q->q.stat - pos;
	if (!eoq)
		pos = q->q.desc;

	len = DIV_ROUND_UP(sizeof(struct chcr_ipsec_aadiv), 16) << 4;
	memset(pos, 0, len);
	aadiv = (struct chcr_ipsec_aadiv *)pos;
	esphdr = (struct ip_esp_hdr *)skb_transport_header(skb);
	iv = skb_transport_header(skb) + sizeof(struct ip_esp_hdr);
	xo = xfrm_offload(skb);

	aadiv->spi = (esphdr->spi);
	seqlo = htonl(esphdr->seq_no);
	seqno = cpu_to_be64(seqlo + ((u64)xo->seq.hi << 32));
	memcpy(aadiv->seq_no, &seqno, 8);
	iv = skb_transport_header(skb) + sizeof(struct ip_esp_hdr);
	memcpy(aadiv->iv, iv, 8);

	if (is_eth_imm(skb, sa_entry) && !skb_is_nonlinear(skb)) {
		sc_imm = (struct ulptx_idata *)(pos +
			  (DIV_ROUND_UP(sizeof(struct chcr_ipsec_aadiv),
					sizeof(__be64)) << 3));
		sc_imm->cmd_more = FILL_CMD_MORE(0);
		sc_imm->len = cpu_to_be32(skb->len);
	}
	pos += len;
	return pos;
}

inline void *copy_cpltx_pktxt(struct sk_buff *skb,
			      struct net_device *dev,
			      void *pos,
			      struct ipsec_sa_entry *sa_entry)
{
	struct cpl_tx_pkt_core *cpl;
	struct sge_eth_txq *q;
	struct adapter *adap;
	struct port_info *pi;
	u32 ctrl0, qidx;
	u64 cntrl = 0;
	int left;

	pi = netdev_priv(dev);
	adap = pi->adapter;
	qidx = skb->queue_mapping;
	q = &adap->sge.ethtxq[qidx + pi->first_qset];

	left = (void *)q->q.stat - pos;
	if (!left)
		pos = q->q.desc;

	cpl = (struct cpl_tx_pkt_core *)pos;

	cntrl = TXPKT_L4CSUM_DIS_F | TXPKT_IPCSUM_DIS_F;
	ctrl0 = TXPKT_OPCODE_V(CPL_TX_PKT_XT) | TXPKT_INTF_V(pi->tx_chan) |
			       TXPKT_PF_V(adap->pf);
	if (skb_vlan_tag_present(skb)) {
		q->vlan_ins++;
		cntrl |= TXPKT_VLAN_VLD_F | TXPKT_VLAN_V(skb_vlan_tag_get(skb));
	}

	cpl->ctrl0 = htonl(ctrl0);
	cpl->pack = htons(0);
	cpl->len = htons(skb->len);
	cpl->ctrl1 = cpu_to_be64(cntrl);

	pos += sizeof(struct cpl_tx_pkt_core);
	/* Copy ESN info for HW */
	if (sa_entry->esn)
		pos = copy_esn_pktxt(skb, dev, pos, sa_entry);
	return pos;
}

inline void *copy_key_cpltx_pktxt(struct sk_buff *skb,
				struct net_device *dev,
				void *pos,
				struct ipsec_sa_entry *sa_entry)
{
	struct _key_ctx *key_ctx;
	int left, eoq, key_len;
	struct sge_eth_txq *q;
	struct adapter *adap;
	struct port_info *pi;
	unsigned int qidx;

	pi = netdev_priv(dev);
	adap = pi->adapter;
	qidx = skb->queue_mapping;
	q = &adap->sge.ethtxq[qidx + pi->first_qset];
	key_len = sa_entry->kctx_len;

	/* end of queue, reset pos to start of queue */
	eoq = (void *)q->q.stat - pos;
	left = eoq;
	if (!eoq) {
		pos = q->q.desc;
		left = 64 * q->q.size;
	}

	/* Copy the Key context header */
	key_ctx = (struct _key_ctx *)pos;
	key_ctx->ctx_hdr = sa_entry->key_ctx_hdr;
	memcpy(key_ctx->salt, sa_entry->salt, MAX_SALT);
	pos += sizeof(struct _key_ctx);
	left -= sizeof(struct _key_ctx);

	if (likely(key_len <= left)) {
		memcpy(key_ctx->key, sa_entry->key, key_len);
		pos += key_len;
	} else {
		memcpy(pos, sa_entry->key, left);
		memcpy(q->q.desc, sa_entry->key + left,
		       key_len - left);
		pos = (u8 *)q->q.desc + (key_len - left);
	}
	/* Copy CPL TX PKT XT */
	pos = copy_cpltx_pktxt(skb, dev, pos, sa_entry);

	return pos;
}

inline void *chcr_crypto_wreq(struct sk_buff *skb,
			       struct net_device *dev,
			       void *pos,
			       int credits,
			       struct ipsec_sa_entry *sa_entry)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	unsigned int ivsize = GCM_ESP_IV_SIZE;
	struct chcr_ipsec_wr *wr;
	bool immediate = false;
	u16 immdatalen = 0;
	unsigned int flits;
	u32 ivinoffset;
	u32 aadstart;
	u32 aadstop;
	u32 ciphstart;
	u16 sc_more = 0;
	u32 ivdrop = 0;
	u32 esnlen = 0;
	u32 wr_mid;
	u16 ndesc;
	int qidx = skb_get_queue_mapping(skb);
	struct sge_eth_txq *q = &adap->sge.ethtxq[qidx + pi->first_qset];
	unsigned int kctx_len = sa_entry->kctx_len;
	int qid = q->q.cntxt_id;

	atomic_inc(&adap->chcr_stats.ipsec_cnt);

	flits = calc_tx_sec_flits(skb, sa_entry, &immediate);
	ndesc = DIV_ROUND_UP(flits, 2);
	if (sa_entry->esn)
		ivdrop = 1;

	if (immediate)
		immdatalen = skb->len;

	if (sa_entry->esn) {
		esnlen = sizeof(struct chcr_ipsec_aadiv);
		if (!skb_is_nonlinear(skb))
			sc_more  = 1;
	}

	/* WR Header */
	wr = (struct chcr_ipsec_wr *)pos;
	wr->wreq.op_to_compl = htonl(FW_WR_OP_V(FW_ULPTX_WR));
	wr_mid = FW_CRYPTO_LOOKASIDE_WR_LEN16_V(ndesc);

	if (unlikely(credits < ETHTXQ_STOP_THRES)) {
		netif_tx_stop_queue(q->txq);
		q->q.stops++;
		if (!q->dbqt)
			wr_mid |= FW_WR_EQUEQ_F | FW_WR_EQUIQ_F;
	}
	wr_mid |= FW_ULPTX_WR_DATA_F;
	wr->wreq.flowid_len16 = htonl(wr_mid);

	/* ULPTX */
	wr->req.ulptx.cmd_dest = FILL_ULPTX_CMD_DEST(pi->port_id, qid);
	wr->req.ulptx.len = htonl(ndesc - 1);

	/* Sub-command */
	wr->req.sc_imm.cmd_more = FILL_CMD_MORE(!immdatalen || sc_more);
	wr->req.sc_imm.len = cpu_to_be32(sizeof(struct cpl_tx_sec_pdu) +
					 sizeof(wr->req.key_ctx) +
					 kctx_len +
					 sizeof(struct cpl_tx_pkt_core) +
					 esnlen +
					 (esnlen ? 0 : immdatalen));

	/* CPL_SEC_PDU */
	ivinoffset = sa_entry->esn ? (ESN_IV_INSERT_OFFSET + 1) :
				     (skb_transport_offset(skb) +
				      sizeof(struct ip_esp_hdr) + 1);
	wr->req.sec_cpl.op_ivinsrtofst = htonl(
				CPL_TX_SEC_PDU_OPCODE_V(CPL_TX_SEC_PDU) |
				CPL_TX_SEC_PDU_CPLLEN_V(2) |
				CPL_TX_SEC_PDU_PLACEHOLDER_V(1) |
				CPL_TX_SEC_PDU_IVINSRTOFST_V(
							     ivinoffset));

	wr->req.sec_cpl.pldlen = htonl(skb->len + esnlen);
	aadstart = sa_entry->esn ? 1 : (skb_transport_offset(skb) + 1);
	aadstop = sa_entry->esn ? ESN_IV_INSERT_OFFSET :
				  (skb_transport_offset(skb) +
				   sizeof(struct ip_esp_hdr));
	ciphstart = skb_transport_offset(skb) + sizeof(struct ip_esp_hdr) +
		    GCM_ESP_IV_SIZE + 1;
	ciphstart += sa_entry->esn ?  esnlen : 0;

	wr->req.sec_cpl.aadstart_cipherstop_hi = FILL_SEC_CPL_CIPHERSTOP_HI(
							aadstart,
							aadstop,
							ciphstart, 0);

	wr->req.sec_cpl.cipherstop_lo_authinsert =
		FILL_SEC_CPL_AUTHINSERT(0, ciphstart,
					sa_entry->authsize,
					 sa_entry->authsize);
	wr->req.sec_cpl.seqno_numivs =
		FILL_SEC_CPL_SCMD0_SEQNO(CHCR_ENCRYPT_OP, 1,
					 CHCR_SCMD_CIPHER_MODE_AES_GCM,
					 CHCR_SCMD_AUTH_MODE_GHASH,
					 sa_entry->hmac_ctrl,
					 ivsize >> 1);
	wr->req.sec_cpl.ivgen_hdrlen =  FILL_SEC_CPL_IVGEN_HDRLEN(0, 0, 1,
								  0, ivdrop, 0);

	pos += sizeof(struct fw_ulptx_wr) +
	       sizeof(struct ulp_txpkt) +
	       sizeof(struct ulptx_idata) +
	       sizeof(struct cpl_tx_sec_pdu);

	pos = copy_key_cpltx_pktxt(skb, dev, pos, sa_entry);

	return pos;
}

/**
 *      flits_to_desc - returns the num of Tx descriptors for the given flits
 *      @n: the number of flits
 *
 *      Returns the number of Tx descriptors needed for the supplied number
 *      of flits.
 */
static inline unsigned int flits_to_desc(unsigned int n)
{
	WARN_ON(n > SGE_MAX_WR_LEN / 8);
	return DIV_ROUND_UP(n, 8);
}

static inline unsigned int txq_avail(const struct sge_txq *q)
{
	return q->size - 1 - q->in_use;
}

static void eth_txq_stop(struct sge_eth_txq *q)
{
	netif_tx_stop_queue(q->txq);
	q->q.stops++;
}

static inline void txq_advance(struct sge_txq *q, unsigned int n)
{
	q->in_use += n;
	q->pidx += n;
	if (q->pidx >= q->size)
		q->pidx -= q->size;
}

/*
 *      chcr_ipsec_xmit called from ULD Tx handler
 */
int chcr_ipsec_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct xfrm_state *x = xfrm_input_state(skb);
	unsigned int last_desc, ndesc, flits = 0;
	struct ipsec_sa_entry *sa_entry;
	u64 *pos, *end, *before, *sgl;
	struct tx_sw_desc *sgl_sdesc;
	int qidx, left, credits;
	bool immediate = false;
	struct sge_eth_txq *q;
	struct adapter *adap;
	struct port_info *pi;
	struct sec_path *sp;

	if (!x->xso.offload_handle)
		return NETDEV_TX_BUSY;

	sa_entry = (struct ipsec_sa_entry *)x->xso.offload_handle;

	sp = skb_sec_path(skb);
	if (sp->len != 1) {
out_free:       dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	pi = netdev_priv(dev);
	adap = pi->adapter;
	qidx = skb->queue_mapping;
	q = &adap->sge.ethtxq[qidx + pi->first_qset];

	cxgb4_reclaim_completed_tx(adap, &q->q, true);

	flits = calc_tx_sec_flits(skb, sa_entry, &immediate);
	ndesc = flits_to_desc(flits);
	credits = txq_avail(&q->q) - ndesc;

	if (unlikely(credits < 0)) {
		eth_txq_stop(q);
		dev_err(adap->pdev_dev,
			"%s: Tx ring %u full while queue awake! cred:%d %d %d flits:%d\n",
			dev->name, qidx, credits, ndesc, txq_avail(&q->q),
			flits);
		return NETDEV_TX_BUSY;
	}

	last_desc = q->q.pidx + ndesc - 1;
	if (last_desc >= q->q.size)
		last_desc -= q->q.size;
	sgl_sdesc = &q->q.sdesc[last_desc];

	if (!immediate &&
	    unlikely(cxgb4_map_skb(adap->pdev_dev, skb, sgl_sdesc->addr) < 0)) {
		memset(sgl_sdesc->addr, 0, sizeof(sgl_sdesc->addr));
		q->mapping_err++;
		goto out_free;
	}

	pos = (u64 *)&q->q.desc[q->q.pidx];
	before = (u64 *)pos;
	end = (u64 *)pos + flits;
	/* Setup IPSec CPL */
	pos = (void *)chcr_crypto_wreq(skb, dev, (void *)pos,
				       credits, sa_entry);
	if (before > (u64 *)pos) {
		left = (u8 *)end - (u8 *)q->q.stat;
		end = (void *)q->q.desc + left;
	}
	if (pos == (u64 *)q->q.stat) {
		left = (u8 *)end - (u8 *)q->q.stat;
		end = (void *)q->q.desc + left;
		pos = (void *)q->q.desc;
	}

	sgl = (void *)pos;
	if (immediate) {
		cxgb4_inline_tx_skb(skb, &q->q, sgl);
		dev_consume_skb_any(skb);
	} else {
		cxgb4_write_sgl(skb, &q->q, (void *)sgl, end,
				0, sgl_sdesc->addr);
		skb_orphan(skb);
		sgl_sdesc->skb = skb;
	}
	txq_advance(&q->q, ndesc);

	cxgb4_ring_tx_db(adap, &q->q, ndesc);
	return NETDEV_TX_OK;
}
