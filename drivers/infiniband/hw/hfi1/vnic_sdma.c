// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2017 - 2018 Intel Corporation.
 */

/*
 * This file contains HFI1 support for VNIC SDMA functionality
 */

#include "sdma.h"
#include "vnic.h"

#define HFI1_VNIC_SDMA_Q_ACTIVE   BIT(0)
#define HFI1_VNIC_SDMA_Q_DEFERRED BIT(1)

#define HFI1_VNIC_TXREQ_NAME_LEN   32
#define HFI1_VNIC_SDMA_DESC_WTRMRK 64

/*
 * struct vnic_txreq - VNIC transmit descriptor
 * @txreq: sdma transmit request
 * @sdma: vnic sdma pointer
 * @skb: skb to send
 * @pad: pad buffer
 * @plen: pad length
 * @pbc_val: pbc value
 */
struct vnic_txreq {
	struct sdma_txreq       txreq;
	struct hfi1_vnic_sdma   *sdma;

	struct sk_buff         *skb;
	unsigned char           pad[HFI1_VNIC_MAX_PAD];
	u16                     plen;
	__le64                  pbc_val;
};

static void vnic_sdma_complete(struct sdma_txreq *txreq,
			       int status)
{
	struct vnic_txreq *tx = container_of(txreq, struct vnic_txreq, txreq);
	struct hfi1_vnic_sdma *vnic_sdma = tx->sdma;

	sdma_txclean(vnic_sdma->dd, txreq);
	dev_kfree_skb_any(tx->skb);
	kmem_cache_free(vnic_sdma->dd->vnic.txreq_cache, tx);
}

static noinline int build_vnic_ulp_payload(struct sdma_engine *sde,
					   struct vnic_txreq *tx)
{
	int i, ret = 0;

	ret = sdma_txadd_kvaddr(
		sde->dd,
		&tx->txreq,
		tx->skb->data,
		skb_headlen(tx->skb));
	if (unlikely(ret))
		goto bail_txadd;

	for (i = 0; i < skb_shinfo(tx->skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(tx->skb)->frags[i];

		/* combine physically continuous fragments later? */
		ret = sdma_txadd_page(sde->dd,
				      &tx->txreq,
				      skb_frag_page(frag),
				      skb_frag_off(frag),
				      skb_frag_size(frag));
		if (unlikely(ret))
			goto bail_txadd;
	}

	if (tx->plen)
		ret = sdma_txadd_kvaddr(sde->dd, &tx->txreq,
					tx->pad + HFI1_VNIC_MAX_PAD - tx->plen,
					tx->plen);

bail_txadd:
	return ret;
}

static int build_vnic_tx_desc(struct sdma_engine *sde,
			      struct vnic_txreq *tx,
			      u64 pbc)
{
	int ret = 0;
	u16 hdrbytes = 2 << 2;  /* PBC */

	ret = sdma_txinit_ahg(
		&tx->txreq,
		0,
		hdrbytes + tx->skb->len + tx->plen,
		0,
		0,
		NULL,
		0,
		vnic_sdma_complete);
	if (unlikely(ret))
		goto bail_txadd;

	/* add pbc */
	tx->pbc_val = cpu_to_le64(pbc);
	ret = sdma_txadd_kvaddr(
		sde->dd,
		&tx->txreq,
		&tx->pbc_val,
		hdrbytes);
	if (unlikely(ret))
		goto bail_txadd;

	/* add the ulp payload */
	ret = build_vnic_ulp_payload(sde, tx);
bail_txadd:
	return ret;
}

/* setup the last plen bypes of pad */
static inline void hfi1_vnic_update_pad(unsigned char *pad, u8 plen)
{
	pad[HFI1_VNIC_MAX_PAD - 1] = plen - OPA_VNIC_ICRC_TAIL_LEN;
}

int hfi1_vnic_send_dma(struct hfi1_devdata *dd, u8 q_idx,
		       struct hfi1_vnic_vport_info *vinfo,
		       struct sk_buff *skb, u64 pbc, u8 plen)
{
	struct hfi1_vnic_sdma *vnic_sdma = &vinfo->sdma[q_idx];
	struct sdma_engine *sde = vnic_sdma->sde;
	struct vnic_txreq *tx;
	int ret = -ECOMM;

	if (unlikely(READ_ONCE(vnic_sdma->state) != HFI1_VNIC_SDMA_Q_ACTIVE))
		goto tx_err;

	if (unlikely(!sde || !sdma_running(sde)))
		goto tx_err;

	tx = kmem_cache_alloc(dd->vnic.txreq_cache, GFP_ATOMIC);
	if (unlikely(!tx)) {
		ret = -ENOMEM;
		goto tx_err;
	}

	tx->sdma = vnic_sdma;
	tx->skb = skb;
	hfi1_vnic_update_pad(tx->pad, plen);
	tx->plen = plen;
	ret = build_vnic_tx_desc(sde, tx, pbc);
	if (unlikely(ret))
		goto free_desc;

	ret = sdma_send_txreq(sde, iowait_get_ib_work(&vnic_sdma->wait),
			      &tx->txreq, vnic_sdma->pkts_sent);
	/* When -ECOMM, sdma callback will be called with ABORT status */
	if (unlikely(ret && unlikely(ret != -ECOMM)))
		goto free_desc;

	if (!ret) {
		vnic_sdma->pkts_sent = true;
		iowait_starve_clear(vnic_sdma->pkts_sent, &vnic_sdma->wait);
	}
	return ret;

free_desc:
	sdma_txclean(dd, &tx->txreq);
	kmem_cache_free(dd->vnic.txreq_cache, tx);
tx_err:
	if (ret != -EBUSY)
		dev_kfree_skb_any(skb);
	else
		vnic_sdma->pkts_sent = false;
	return ret;
}

/*
 * hfi1_vnic_sdma_sleep - vnic sdma sleep function
 *
 * This function gets called from sdma_send_txreq() when there are not enough
 * sdma descriptors available to send the packet. It adds Tx queue's wait
 * structure to sdma engine's dmawait list to be woken up when descriptors
 * become available.
 */
static int hfi1_vnic_sdma_sleep(struct sdma_engine *sde,
				struct iowait_work *wait,
				struct sdma_txreq *txreq,
				uint seq,
				bool pkts_sent)
{
	struct hfi1_vnic_sdma *vnic_sdma =
		container_of(wait->iow, struct hfi1_vnic_sdma, wait);

	write_seqlock(&sde->waitlock);
	if (sdma_progress(sde, seq, txreq)) {
		write_sequnlock(&sde->waitlock);
		return -EAGAIN;
	}

	vnic_sdma->state = HFI1_VNIC_SDMA_Q_DEFERRED;
	if (list_empty(&vnic_sdma->wait.list)) {
		iowait_get_priority(wait->iow);
		iowait_queue(pkts_sent, wait->iow, &sde->dmawait);
	}
	write_sequnlock(&sde->waitlock);
	return -EBUSY;
}

/*
 * hfi1_vnic_sdma_wakeup - vnic sdma wakeup function
 *
 * This function gets called when SDMA descriptors becomes available and Tx
 * queue's wait structure was previously added to sdma engine's dmawait list.
 * It notifies the upper driver about Tx queue wakeup.
 */
static void hfi1_vnic_sdma_wakeup(struct iowait *wait, int reason)
{
	struct hfi1_vnic_sdma *vnic_sdma =
		container_of(wait, struct hfi1_vnic_sdma, wait);
	struct hfi1_vnic_vport_info *vinfo = vnic_sdma->vinfo;

	vnic_sdma->state = HFI1_VNIC_SDMA_Q_ACTIVE;
	if (__netif_subqueue_stopped(vinfo->netdev, vnic_sdma->q_idx))
		netif_wake_subqueue(vinfo->netdev, vnic_sdma->q_idx);
};

inline bool hfi1_vnic_sdma_write_avail(struct hfi1_vnic_vport_info *vinfo,
				       u8 q_idx)
{
	struct hfi1_vnic_sdma *vnic_sdma = &vinfo->sdma[q_idx];

	return (READ_ONCE(vnic_sdma->state) == HFI1_VNIC_SDMA_Q_ACTIVE);
}

void hfi1_vnic_sdma_init(struct hfi1_vnic_vport_info *vinfo)
{
	int i;

	for (i = 0; i < vinfo->num_tx_q; i++) {
		struct hfi1_vnic_sdma *vnic_sdma = &vinfo->sdma[i];

		iowait_init(&vnic_sdma->wait, 0, NULL, NULL,
			    hfi1_vnic_sdma_sleep,
			    hfi1_vnic_sdma_wakeup, NULL, NULL);
		vnic_sdma->sde = &vinfo->dd->per_sdma[i];
		vnic_sdma->dd = vinfo->dd;
		vnic_sdma->vinfo = vinfo;
		vnic_sdma->q_idx = i;
		vnic_sdma->state = HFI1_VNIC_SDMA_Q_ACTIVE;

		/* Add a free descriptor watermark for wakeups */
		if (vnic_sdma->sde->descq_cnt > HFI1_VNIC_SDMA_DESC_WTRMRK) {
			struct iowait_work *work;

			INIT_LIST_HEAD(&vnic_sdma->stx.list);
			vnic_sdma->stx.num_desc = HFI1_VNIC_SDMA_DESC_WTRMRK;
			work = iowait_get_ib_work(&vnic_sdma->wait);
			list_add_tail(&vnic_sdma->stx.list, &work->tx_head);
		}
	}
}

int hfi1_vnic_txreq_init(struct hfi1_devdata *dd)
{
	char buf[HFI1_VNIC_TXREQ_NAME_LEN];

	snprintf(buf, sizeof(buf), "hfi1_%u_vnic_txreq_cache", dd->unit);
	dd->vnic.txreq_cache = kmem_cache_create(buf,
						 sizeof(struct vnic_txreq),
						 0, SLAB_HWCACHE_ALIGN,
						 NULL);
	if (!dd->vnic.txreq_cache)
		return -ENOMEM;
	return 0;
}

void hfi1_vnic_txreq_deinit(struct hfi1_devdata *dd)
{
	kmem_cache_destroy(dd->vnic.txreq_cache);
	dd->vnic.txreq_cache = NULL;
}
