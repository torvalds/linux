// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

#include "ce.h"
#include "coredump.h"
#include "debug.h"
#include "hif.h"
#include "htc.h"
#include "snoc.h"

#define ATH10K_SNOC_RX_POST_RETRY_MS 50
#define CE_POLL_PIPE 4
#define ATH10K_SNOC_WAKE_IRQ 2

static char *const ce_name[] = {
	"WLAN_CE_0",
	"WLAN_CE_1",
	"WLAN_CE_2",
	"WLAN_CE_3",
	"WLAN_CE_4",
	"WLAN_CE_5",
	"WLAN_CE_6",
	"WLAN_CE_7",
	"WLAN_CE_8",
	"WLAN_CE_9",
	"WLAN_CE_10",
	"WLAN_CE_11",
};

static const char * const ath10k_regulators[] = {
	"vdd-0.8-cx-mx",
	"vdd-1.8-xo",
	"vdd-1.3-rfa",
	"vdd-3.3-ch0",
};

static const char * const ath10k_clocks[] = {
	"cxo_ref_clk_pin",
};

static void ath10k_snoc_htc_tx_cb(struct ath10k_ce_pipe *ce_state);
static void ath10k_snoc_htt_tx_cb(struct ath10k_ce_pipe *ce_state);
static void ath10k_snoc_htc_rx_cb(struct ath10k_ce_pipe *ce_state);
static void ath10k_snoc_htt_rx_cb(struct ath10k_ce_pipe *ce_state);
static void ath10k_snoc_htt_htc_rx_cb(struct ath10k_ce_pipe *ce_state);
static void ath10k_snoc_pktlog_rx_cb(struct ath10k_ce_pipe *ce_state);

static const struct ath10k_snoc_drv_priv drv_priv = {
	.hw_rev = ATH10K_HW_WCN3990,
	.dma_mask = DMA_BIT_MASK(35),
	.msa_size = 0x100000,
};

#define WCN3990_SRC_WR_IDX_OFFSET 0x3C
#define WCN3990_DST_WR_IDX_OFFSET 0x40

static struct ath10k_shadow_reg_cfg target_shadow_reg_cfg_map[] = {
		{
			.ce_id = __cpu_to_le16(0),
			.reg_offset = __cpu_to_le16(WCN3990_SRC_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(3),
			.reg_offset = __cpu_to_le16(WCN3990_SRC_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(4),
			.reg_offset = __cpu_to_le16(WCN3990_SRC_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(5),
			.reg_offset =  __cpu_to_le16(WCN3990_SRC_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(7),
			.reg_offset = __cpu_to_le16(WCN3990_SRC_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(1),
			.reg_offset = __cpu_to_le16(WCN3990_DST_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(2),
			.reg_offset =  __cpu_to_le16(WCN3990_DST_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(7),
			.reg_offset =  __cpu_to_le16(WCN3990_DST_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(8),
			.reg_offset =  __cpu_to_le16(WCN3990_DST_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(9),
			.reg_offset = __cpu_to_le16(WCN3990_DST_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(10),
			.reg_offset =  __cpu_to_le16(WCN3990_DST_WR_IDX_OFFSET),
		},

		{
			.ce_id = __cpu_to_le16(11),
			.reg_offset = __cpu_to_le16(WCN3990_DST_WR_IDX_OFFSET),
		},
};

static struct ce_attr host_ce_config_wlan[] = {
	/* CE0: host->target HTC control streams */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 16,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = ath10k_snoc_htc_tx_cb,
	},

	/* CE1: target->host HTT + HTC control */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath10k_snoc_htt_htc_rx_cb,
	},

	/* CE2: target->host WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 64,
		.recv_cb = ath10k_snoc_htc_rx_cb,
	},

	/* CE3: host->target WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = ath10k_snoc_htc_tx_cb,
	},

	/* CE4: host->target HTT */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 2048,
		.src_sz_max = 256,
		.dest_nentries = 0,
		.send_cb = ath10k_snoc_htt_tx_cb,
	},

	/* CE5: target->host HTT (ipa_uc->target ) */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 512,
		.dest_nentries = 512,
		.recv_cb = ath10k_snoc_htt_rx_cb,
	},

	/* CE6: target autonomous hif_memcpy */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE7: ce_diag, the Diagnostic Window */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 2,
		.src_sz_max = 2048,
		.dest_nentries = 2,
	},

	/* CE8: Target to uMC */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 128,
	},

	/* CE9 target->host HTT */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath10k_snoc_htt_htc_rx_cb,
	},

	/* CE10: target->host HTT */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath10k_snoc_htt_htc_rx_cb,
	},

	/* CE11: target -> host PKTLOG */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath10k_snoc_pktlog_rx_cb,
	},
};

static struct ce_pipe_config target_ce_config_wlan[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.pipenum = __cpu_to_le32(0),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE1: target->host HTT + HTC control */
	{
		.pipenum = __cpu_to_le32(1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE2: target->host WMI */
	{
		.pipenum = __cpu_to_le32(2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(64),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE3: host->target WMI */
	{
		.pipenum = __cpu_to_le32(3),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE4: host->target HTT */
	{
		.pipenum = __cpu_to_le32(4),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(256),
		.nbytes_max = __cpu_to_le32(256),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE5: target->host HTT (HIF->HTT) */
	{
		.pipenum = __cpu_to_le32(5),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(1024),
		.nbytes_max = __cpu_to_le32(64),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE6: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = __cpu_to_le32(6),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE7 used only by Host */
	{
		.pipenum = __cpu_to_le32(7),
		.pipedir = __cpu_to_le32(4),
		.nentries = __cpu_to_le32(0),
		.nbytes_max = __cpu_to_le32(0),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE8 Target to uMC */
	{
		.pipenum = __cpu_to_le32(8),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(0),
		.reserved = __cpu_to_le32(0),
	},

	/* CE9 target->host HTT */
	{
		.pipenum = __cpu_to_le32(9),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE10 target->host HTT */
	{
		.pipenum = __cpu_to_le32(10),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE11 target autonomous qcache memcpy */
	{
		.pipenum = __cpu_to_le32(11),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
};

static struct service_to_pipe target_service_to_ce_map_wlan[] = {
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(0),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{ /* not used */
		__cpu_to_le32(ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(0),
	},
	{ /* not used */
		__cpu_to_le32(ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(4),
	},
	{
		__cpu_to_le32(ATH10K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(1),
	},
	{ /* not used */
		__cpu_to_le32(ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS),
		__cpu_to_le32(PIPEDIR_OUT),
		__cpu_to_le32(5),
	},
	{ /* in = DL = target -> host */
		__cpu_to_le32(ATH10K_HTC_SVC_ID_HTT_DATA2_MSG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(9),
	},
	{ /* in = DL = target -> host */
		__cpu_to_le32(ATH10K_HTC_SVC_ID_HTT_DATA3_MSG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(10),
	},
	{ /* in = DL = target -> host pktlog */
		__cpu_to_le32(ATH10K_HTC_SVC_ID_HTT_LOG_MSG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(11),
	},
	/* (Additions here) */

	{ /* must be last */
		__cpu_to_le32(0),
		__cpu_to_le32(0),
		__cpu_to_le32(0),
	},
};

static void ath10k_snoc_write32(struct ath10k *ar, u32 offset, u32 value)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);

	iowrite32(value, ar_snoc->mem + offset);
}

static u32 ath10k_snoc_read32(struct ath10k *ar, u32 offset)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	u32 val;

	val = ioread32(ar_snoc->mem + offset);

	return val;
}

static int __ath10k_snoc_rx_post_buf(struct ath10k_snoc_pipe *pipe)
{
	struct ath10k_ce_pipe *ce_pipe = pipe->ce_hdl;
	struct ath10k *ar = pipe->hif_ce_state;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct sk_buff *skb;
	dma_addr_t paddr;
	int ret;

	skb = dev_alloc_skb(pipe->buf_sz);
	if (!skb)
		return -ENOMEM;

	WARN_ONCE((unsigned long)skb->data & 3, "unaligned skb");

	paddr = dma_map_single(ar->dev, skb->data,
			       skb->len + skb_tailroom(skb),
			       DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(ar->dev, paddr))) {
		ath10k_warn(ar, "failed to dma map snoc rx buf\n");
		dev_kfree_skb_any(skb);
		return -EIO;
	}

	ATH10K_SKB_RXCB(skb)->paddr = paddr;

	spin_lock_bh(&ce->ce_lock);
	ret = ce_pipe->ops->ce_rx_post_buf(ce_pipe, skb, paddr);
	spin_unlock_bh(&ce->ce_lock);
	if (ret) {
		dma_unmap_single(ar->dev, paddr, skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
		return ret;
	}

	return 0;
}

static void ath10k_snoc_rx_post_pipe(struct ath10k_snoc_pipe *pipe)
{
	struct ath10k *ar = pipe->hif_ce_state;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_ce_pipe *ce_pipe = pipe->ce_hdl;
	int ret, num;

	if (pipe->buf_sz == 0)
		return;

	if (!ce_pipe->dest_ring)
		return;

	spin_lock_bh(&ce->ce_lock);
	num = __ath10k_ce_rx_num_free_bufs(ce_pipe);
	spin_unlock_bh(&ce->ce_lock);
	while (num--) {
		ret = __ath10k_snoc_rx_post_buf(pipe);
		if (ret) {
			if (ret == -ENOSPC)
				break;
			ath10k_warn(ar, "failed to post rx buf: %d\n", ret);
			mod_timer(&ar_snoc->rx_post_retry, jiffies +
				  ATH10K_SNOC_RX_POST_RETRY_MS);
			break;
		}
	}
}

static void ath10k_snoc_rx_post(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int i;

	for (i = 0; i < CE_COUNT; i++)
		ath10k_snoc_rx_post_pipe(&ar_snoc->pipe_info[i]);
}

static void ath10k_snoc_process_rx_cb(struct ath10k_ce_pipe *ce_state,
				      void (*callback)(struct ath10k *ar,
						       struct sk_buff *skb))
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_snoc_pipe *pipe_info =  &ar_snoc->pipe_info[ce_state->id];
	struct sk_buff *skb;
	struct sk_buff_head list;
	void *transfer_context;
	unsigned int nbytes, max_nbytes;

	__skb_queue_head_init(&list);
	while (ath10k_ce_completed_recv_next(ce_state, &transfer_context,
					     &nbytes) == 0) {
		skb = transfer_context;
		max_nbytes = skb->len + skb_tailroom(skb);
		dma_unmap_single(ar->dev, ATH10K_SKB_RXCB(skb)->paddr,
				 max_nbytes, DMA_FROM_DEVICE);

		if (unlikely(max_nbytes < nbytes)) {
			ath10k_warn(ar, "rxed more than expected (nbytes %d, max %d)",
				    nbytes, max_nbytes);
			dev_kfree_skb_any(skb);
			continue;
		}

		skb_put(skb, nbytes);
		__skb_queue_tail(&list, skb);
	}

	while ((skb = __skb_dequeue(&list))) {
		ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc rx ce pipe %d len %d\n",
			   ce_state->id, skb->len);

		callback(ar, skb);
	}

	ath10k_snoc_rx_post_pipe(pipe_info);
}

static void ath10k_snoc_htc_rx_cb(struct ath10k_ce_pipe *ce_state)
{
	ath10k_snoc_process_rx_cb(ce_state, ath10k_htc_rx_completion_handler);
}

static void ath10k_snoc_htt_htc_rx_cb(struct ath10k_ce_pipe *ce_state)
{
	/* CE4 polling needs to be done whenever CE pipe which transports
	 * HTT Rx (target->host) is processed.
	 */
	ath10k_ce_per_engine_service(ce_state->ar, CE_POLL_PIPE);

	ath10k_snoc_process_rx_cb(ce_state, ath10k_htc_rx_completion_handler);
}

/* Called by lower (CE) layer when data is received from the Target.
 * WCN3990 firmware uses separate CE(CE11) to transfer pktlog data.
 */
static void ath10k_snoc_pktlog_rx_cb(struct ath10k_ce_pipe *ce_state)
{
	ath10k_snoc_process_rx_cb(ce_state, ath10k_htc_rx_completion_handler);
}

static void ath10k_snoc_htt_rx_deliver(struct ath10k *ar, struct sk_buff *skb)
{
	skb_pull(skb, sizeof(struct ath10k_htc_hdr));
	ath10k_htt_t2h_msg_handler(ar, skb);
}

static void ath10k_snoc_htt_rx_cb(struct ath10k_ce_pipe *ce_state)
{
	ath10k_ce_per_engine_service(ce_state->ar, CE_POLL_PIPE);
	ath10k_snoc_process_rx_cb(ce_state, ath10k_snoc_htt_rx_deliver);
}

static void ath10k_snoc_rx_replenish_retry(struct timer_list *t)
{
	struct ath10k_snoc *ar_snoc = from_timer(ar_snoc, t, rx_post_retry);
	struct ath10k *ar = ar_snoc->ar;

	ath10k_snoc_rx_post(ar);
}

static void ath10k_snoc_htc_tx_cb(struct ath10k_ce_pipe *ce_state)
{
	struct ath10k *ar = ce_state->ar;
	struct sk_buff_head list;
	struct sk_buff *skb;

	__skb_queue_head_init(&list);
	while (ath10k_ce_completed_send_next(ce_state, (void **)&skb) == 0) {
		if (!skb)
			continue;

		__skb_queue_tail(&list, skb);
	}

	while ((skb = __skb_dequeue(&list)))
		ath10k_htc_tx_completion_handler(ar, skb);
}

static void ath10k_snoc_htt_tx_cb(struct ath10k_ce_pipe *ce_state)
{
	struct ath10k *ar = ce_state->ar;
	struct sk_buff *skb;

	while (ath10k_ce_completed_send_next(ce_state, (void **)&skb) == 0) {
		if (!skb)
			continue;

		dma_unmap_single(ar->dev, ATH10K_SKB_CB(skb)->paddr,
				 skb->len, DMA_TO_DEVICE);
		ath10k_htt_hif_tx_complete(ar, skb);
	}
}

static int ath10k_snoc_hif_tx_sg(struct ath10k *ar, u8 pipe_id,
				 struct ath10k_hif_sg_item *items, int n_items)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_snoc_pipe *snoc_pipe;
	struct ath10k_ce_pipe *ce_pipe;
	int err, i = 0;

	snoc_pipe = &ar_snoc->pipe_info[pipe_id];
	ce_pipe = snoc_pipe->ce_hdl;
	spin_lock_bh(&ce->ce_lock);

	for (i = 0; i < n_items - 1; i++) {
		ath10k_dbg(ar, ATH10K_DBG_SNOC,
			   "snoc tx item %d paddr %pad len %d n_items %d\n",
			   i, &items[i].paddr, items[i].len, n_items);

		err = ath10k_ce_send_nolock(ce_pipe,
					    items[i].transfer_context,
					    items[i].paddr,
					    items[i].len,
					    items[i].transfer_id,
					    CE_SEND_FLAG_GATHER);
		if (err)
			goto err;
	}

	ath10k_dbg(ar, ATH10K_DBG_SNOC,
		   "snoc tx item %d paddr %pad len %d n_items %d\n",
		   i, &items[i].paddr, items[i].len, n_items);

	err = ath10k_ce_send_nolock(ce_pipe,
				    items[i].transfer_context,
				    items[i].paddr,
				    items[i].len,
				    items[i].transfer_id,
				    0);
	if (err)
		goto err;

	spin_unlock_bh(&ce->ce_lock);

	return 0;

err:
	for (; i > 0; i--)
		__ath10k_ce_send_revert(ce_pipe);

	spin_unlock_bh(&ce->ce_lock);
	return err;
}

static int ath10k_snoc_hif_get_target_info(struct ath10k *ar,
					   struct bmi_target_info *target_info)
{
	target_info->version = ATH10K_HW_WCN3990;
	target_info->type = ATH10K_HW_WCN3990;

	return 0;
}

static u16 ath10k_snoc_hif_get_free_queue_number(struct ath10k *ar, u8 pipe)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "hif get free queue number\n");

	return ath10k_ce_num_free_src_entries(ar_snoc->pipe_info[pipe].ce_hdl);
}

static void ath10k_snoc_hif_send_complete_check(struct ath10k *ar, u8 pipe,
						int force)
{
	int resources;

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc hif send complete check\n");

	if (!force) {
		resources = ath10k_snoc_hif_get_free_queue_number(ar, pipe);

		if (resources > (host_ce_config_wlan[pipe].src_nentries >> 1))
			return;
	}
	ath10k_ce_per_engine_service(ar, pipe);
}

static int ath10k_snoc_hif_map_service_to_pipe(struct ath10k *ar,
					       u16 service_id,
					       u8 *ul_pipe, u8 *dl_pipe)
{
	const struct service_to_pipe *entry;
	bool ul_set = false, dl_set = false;
	int i;

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc hif map service\n");

	for (i = 0; i < ARRAY_SIZE(target_service_to_ce_map_wlan); i++) {
		entry = &target_service_to_ce_map_wlan[i];

		if (__le32_to_cpu(entry->service_id) != service_id)
			continue;

		switch (__le32_to_cpu(entry->pipedir)) {
		case PIPEDIR_NONE:
			break;
		case PIPEDIR_IN:
			WARN_ON(dl_set);
			*dl_pipe = __le32_to_cpu(entry->pipenum);
			dl_set = true;
			break;
		case PIPEDIR_OUT:
			WARN_ON(ul_set);
			*ul_pipe = __le32_to_cpu(entry->pipenum);
			ul_set = true;
			break;
		case PIPEDIR_INOUT:
			WARN_ON(dl_set);
			WARN_ON(ul_set);
			*dl_pipe = __le32_to_cpu(entry->pipenum);
			*ul_pipe = __le32_to_cpu(entry->pipenum);
			dl_set = true;
			ul_set = true;
			break;
		}
	}

	if (!ul_set || !dl_set)
		return -ENOENT;

	return 0;
}

static void ath10k_snoc_hif_get_default_pipe(struct ath10k *ar,
					     u8 *ul_pipe, u8 *dl_pipe)
{
	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc hif get default pipe\n");

	(void)ath10k_snoc_hif_map_service_to_pipe(ar,
						 ATH10K_HTC_SVC_ID_RSVD_CTRL,
						 ul_pipe, dl_pipe);
}

static inline void ath10k_snoc_irq_disable(struct ath10k *ar)
{
	ath10k_ce_disable_interrupts(ar);
}

static inline void ath10k_snoc_irq_enable(struct ath10k *ar)
{
	ath10k_ce_enable_interrupts(ar);
}

static void ath10k_snoc_rx_pipe_cleanup(struct ath10k_snoc_pipe *snoc_pipe)
{
	struct ath10k_ce_pipe *ce_pipe;
	struct ath10k_ce_ring *ce_ring;
	struct sk_buff *skb;
	struct ath10k *ar;
	int i;

	ar = snoc_pipe->hif_ce_state;
	ce_pipe = snoc_pipe->ce_hdl;
	ce_ring = ce_pipe->dest_ring;

	if (!ce_ring)
		return;

	if (!snoc_pipe->buf_sz)
		return;

	for (i = 0; i < ce_ring->nentries; i++) {
		skb = ce_ring->per_transfer_context[i];
		if (!skb)
			continue;

		ce_ring->per_transfer_context[i] = NULL;

		dma_unmap_single(ar->dev, ATH10K_SKB_RXCB(skb)->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
	}
}

static void ath10k_snoc_tx_pipe_cleanup(struct ath10k_snoc_pipe *snoc_pipe)
{
	struct ath10k_ce_pipe *ce_pipe;
	struct ath10k_ce_ring *ce_ring;
	struct sk_buff *skb;
	struct ath10k *ar;
	int i;

	ar = snoc_pipe->hif_ce_state;
	ce_pipe = snoc_pipe->ce_hdl;
	ce_ring = ce_pipe->src_ring;

	if (!ce_ring)
		return;

	if (!snoc_pipe->buf_sz)
		return;

	for (i = 0; i < ce_ring->nentries; i++) {
		skb = ce_ring->per_transfer_context[i];
		if (!skb)
			continue;

		ce_ring->per_transfer_context[i] = NULL;

		ath10k_htc_tx_completion_handler(ar, skb);
	}
}

static void ath10k_snoc_buffer_cleanup(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_snoc_pipe *pipe_info;
	int pipe_num;

	del_timer_sync(&ar_snoc->rx_post_retry);
	for (pipe_num = 0; pipe_num < CE_COUNT; pipe_num++) {
		pipe_info = &ar_snoc->pipe_info[pipe_num];
		ath10k_snoc_rx_pipe_cleanup(pipe_info);
		ath10k_snoc_tx_pipe_cleanup(pipe_info);
	}
}

static void ath10k_snoc_hif_stop(struct ath10k *ar)
{
	if (!test_bit(ATH10K_FLAG_CRASH_FLUSH, &ar->dev_flags))
		ath10k_snoc_irq_disable(ar);

	napi_synchronize(&ar->napi);
	napi_disable(&ar->napi);
	ath10k_snoc_buffer_cleanup(ar);
	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif stop\n");
}

static int ath10k_snoc_hif_start(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);

	napi_enable(&ar->napi);
	ath10k_snoc_irq_enable(ar);
	ath10k_snoc_rx_post(ar);

	clear_bit(ATH10K_SNOC_FLAG_RECOVERY, &ar_snoc->flags);

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif start\n");

	return 0;
}

static int ath10k_snoc_init_pipes(struct ath10k *ar)
{
	int i, ret;

	for (i = 0; i < CE_COUNT; i++) {
		ret = ath10k_ce_init_pipe(ar, i, &host_ce_config_wlan[i]);
		if (ret) {
			ath10k_err(ar, "failed to initialize copy engine pipe %d: %d\n",
				   i, ret);
			return ret;
		}
	}

	return 0;
}

static int ath10k_snoc_wlan_enable(struct ath10k *ar,
				   enum ath10k_firmware_mode fw_mode)
{
	struct ath10k_tgt_pipe_cfg tgt_cfg[CE_COUNT_MAX];
	struct ath10k_qmi_wlan_enable_cfg cfg;
	enum wlfw_driver_mode_enum_v01 mode;
	int pipe_num;

	for (pipe_num = 0; pipe_num < CE_COUNT_MAX; pipe_num++) {
		tgt_cfg[pipe_num].pipe_num =
				target_ce_config_wlan[pipe_num].pipenum;
		tgt_cfg[pipe_num].pipe_dir =
				target_ce_config_wlan[pipe_num].pipedir;
		tgt_cfg[pipe_num].nentries =
				target_ce_config_wlan[pipe_num].nentries;
		tgt_cfg[pipe_num].nbytes_max =
				target_ce_config_wlan[pipe_num].nbytes_max;
		tgt_cfg[pipe_num].flags =
				target_ce_config_wlan[pipe_num].flags;
		tgt_cfg[pipe_num].reserved = 0;
	}

	cfg.num_ce_tgt_cfg = sizeof(target_ce_config_wlan) /
				sizeof(struct ath10k_tgt_pipe_cfg);
	cfg.ce_tgt_cfg = (struct ath10k_tgt_pipe_cfg *)
		&tgt_cfg;
	cfg.num_ce_svc_pipe_cfg = sizeof(target_service_to_ce_map_wlan) /
				  sizeof(struct ath10k_svc_pipe_cfg);
	cfg.ce_svc_cfg = (struct ath10k_svc_pipe_cfg *)
		&target_service_to_ce_map_wlan;
	cfg.num_shadow_reg_cfg = ARRAY_SIZE(target_shadow_reg_cfg_map);
	cfg.shadow_reg_cfg = (struct ath10k_shadow_reg_cfg *)
		&target_shadow_reg_cfg_map;

	switch (fw_mode) {
	case ATH10K_FIRMWARE_MODE_NORMAL:
		mode = QMI_WLFW_MISSION_V01;
		break;
	case ATH10K_FIRMWARE_MODE_UTF:
		mode = QMI_WLFW_FTM_V01;
		break;
	default:
		ath10k_err(ar, "invalid firmware mode %d\n", fw_mode);
		return -EINVAL;
	}

	return ath10k_qmi_wlan_enable(ar, &cfg, mode,
				       NULL);
}

static void ath10k_snoc_wlan_disable(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);

	/* If both ATH10K_FLAG_CRASH_FLUSH and ATH10K_SNOC_FLAG_RECOVERY
	 * flags are not set, it means that the driver has restarted
	 * due to a crash inject via debugfs. In this case, the driver
	 * needs to restart the firmware and hence send qmi wlan disable,
	 * during the driver restart sequence.
	 */
	if (!test_bit(ATH10K_FLAG_CRASH_FLUSH, &ar->dev_flags) ||
	    !test_bit(ATH10K_SNOC_FLAG_RECOVERY, &ar_snoc->flags))
		ath10k_qmi_wlan_disable(ar);
}

static void ath10k_snoc_hif_power_down(struct ath10k *ar)
{
	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif power down\n");

	ath10k_snoc_wlan_disable(ar);
	ath10k_ce_free_rri(ar);
}

static int ath10k_snoc_hif_power_up(struct ath10k *ar,
				    enum ath10k_firmware_mode fw_mode)
{
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "%s:WCN3990 driver state = %d\n",
		   __func__, ar->state);

	ret = ath10k_snoc_wlan_enable(ar, fw_mode);
	if (ret) {
		ath10k_err(ar, "failed to enable wcn3990: %d\n", ret);
		return ret;
	}

	ath10k_ce_alloc_rri(ar);

	ret = ath10k_snoc_init_pipes(ar);
	if (ret) {
		ath10k_err(ar, "failed to initialize CE: %d\n", ret);
		goto err_wlan_enable;
	}

	return 0;

err_wlan_enable:
	ath10k_snoc_wlan_disable(ar);

	return ret;
}

static int ath10k_snoc_hif_set_target_log_mode(struct ath10k *ar,
					       u8 fw_log_mode)
{
	u8 fw_dbg_mode;

	if (fw_log_mode)
		fw_dbg_mode = ATH10K_ENABLE_FW_LOG_CE;
	else
		fw_dbg_mode = ATH10K_ENABLE_FW_LOG_DIAG;

	return ath10k_qmi_set_fw_log_mode(ar, fw_dbg_mode);
}

#ifdef CONFIG_PM
static int ath10k_snoc_hif_suspend(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int ret;

	if (!device_may_wakeup(ar->dev))
		return -EPERM;

	ret = enable_irq_wake(ar_snoc->ce_irqs[ATH10K_SNOC_WAKE_IRQ].irq_line);
	if (ret) {
		ath10k_err(ar, "failed to enable wakeup irq :%d\n", ret);
		return ret;
	}

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc device suspended\n");

	return ret;
}

static int ath10k_snoc_hif_resume(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int ret;

	if (!device_may_wakeup(ar->dev))
		return -EPERM;

	ret = disable_irq_wake(ar_snoc->ce_irqs[ATH10K_SNOC_WAKE_IRQ].irq_line);
	if (ret) {
		ath10k_err(ar, "failed to disable wakeup irq: %d\n", ret);
		return ret;
	}

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc device resumed\n");

	return ret;
}
#endif

static const struct ath10k_hif_ops ath10k_snoc_hif_ops = {
	.read32		= ath10k_snoc_read32,
	.write32	= ath10k_snoc_write32,
	.start		= ath10k_snoc_hif_start,
	.stop		= ath10k_snoc_hif_stop,
	.map_service_to_pipe	= ath10k_snoc_hif_map_service_to_pipe,
	.get_default_pipe	= ath10k_snoc_hif_get_default_pipe,
	.power_up		= ath10k_snoc_hif_power_up,
	.power_down		= ath10k_snoc_hif_power_down,
	.tx_sg			= ath10k_snoc_hif_tx_sg,
	.send_complete_check	= ath10k_snoc_hif_send_complete_check,
	.get_free_queue_number	= ath10k_snoc_hif_get_free_queue_number,
	.get_target_info	= ath10k_snoc_hif_get_target_info,
	.set_target_log_mode    = ath10k_snoc_hif_set_target_log_mode,

#ifdef CONFIG_PM
	.suspend                = ath10k_snoc_hif_suspend,
	.resume                 = ath10k_snoc_hif_resume,
#endif
};

static const struct ath10k_bus_ops ath10k_snoc_bus_ops = {
	.read32		= ath10k_snoc_read32,
	.write32	= ath10k_snoc_write32,
};

static int ath10k_snoc_get_ce_id_from_irq(struct ath10k *ar, int irq)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int i;

	for (i = 0; i < CE_COUNT_MAX; i++) {
		if (ar_snoc->ce_irqs[i].irq_line == irq)
			return i;
	}
	ath10k_err(ar, "No matching CE id for irq %d\n", irq);

	return -EINVAL;
}

static irqreturn_t ath10k_snoc_per_engine_handler(int irq, void *arg)
{
	struct ath10k *ar = arg;
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int ce_id = ath10k_snoc_get_ce_id_from_irq(ar, irq);

	if (ce_id < 0 || ce_id >= ARRAY_SIZE(ar_snoc->pipe_info)) {
		ath10k_warn(ar, "unexpected/invalid irq %d ce_id %d\n", irq,
			    ce_id);
		return IRQ_HANDLED;
	}

	ath10k_snoc_irq_disable(ar);
	napi_schedule(&ar->napi);

	return IRQ_HANDLED;
}

static int ath10k_snoc_napi_poll(struct napi_struct *ctx, int budget)
{
	struct ath10k *ar = container_of(ctx, struct ath10k, napi);
	int done = 0;

	if (test_bit(ATH10K_FLAG_CRASH_FLUSH, &ar->dev_flags)) {
		napi_complete(ctx);
		return done;
	}

	ath10k_ce_per_engine_service_any(ar);
	done = ath10k_htt_txrx_compl_task(ar, budget);

	if (done < budget) {
		napi_complete(ctx);
		ath10k_snoc_irq_enable(ar);
	}

	return done;
}

static void ath10k_snoc_init_napi(struct ath10k *ar)
{
	netif_napi_add(&ar->napi_dev, &ar->napi, ath10k_snoc_napi_poll,
		       ATH10K_NAPI_BUDGET);
}

static int ath10k_snoc_request_irq(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int irqflags = IRQF_TRIGGER_RISING;
	int ret, id;

	for (id = 0; id < CE_COUNT_MAX; id++) {
		ret = request_irq(ar_snoc->ce_irqs[id].irq_line,
				  ath10k_snoc_per_engine_handler,
				  irqflags, ce_name[id], ar);
		if (ret) {
			ath10k_err(ar,
				   "failed to register IRQ handler for CE %d: %d",
				   id, ret);
			goto err_irq;
		}
	}

	return 0;

err_irq:
	for (id -= 1; id >= 0; id--)
		free_irq(ar_snoc->ce_irqs[id].irq_line, ar);

	return ret;
}

static void ath10k_snoc_free_irq(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int id;

	for (id = 0; id < CE_COUNT_MAX; id++)
		free_irq(ar_snoc->ce_irqs[id].irq_line, ar);
}

static int ath10k_snoc_resource_init(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct platform_device *pdev;
	struct resource *res;
	int i, ret = 0;

	pdev = ar_snoc->dev;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "membase");
	if (!res) {
		ath10k_err(ar, "Memory base not found in DT\n");
		return -EINVAL;
	}

	ar_snoc->mem_pa = res->start;
	ar_snoc->mem = devm_ioremap(&pdev->dev, ar_snoc->mem_pa,
				    resource_size(res));
	if (!ar_snoc->mem) {
		ath10k_err(ar, "Memory base ioremap failed with physical address %pa\n",
			   &ar_snoc->mem_pa);
		return -EINVAL;
	}

	for (i = 0; i < CE_COUNT; i++) {
		res = platform_get_resource(ar_snoc->dev, IORESOURCE_IRQ, i);
		if (!res) {
			ath10k_err(ar, "failed to get IRQ%d\n", i);
			ret = -ENODEV;
			goto out;
		}
		ar_snoc->ce_irqs[i].irq_line = res->start;
	}

	ret = device_property_read_u32(&pdev->dev, "qcom,xo-cal-data",
				       &ar_snoc->xo_cal_data);
	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc xo-cal-data return %d\n", ret);
	if (ret == 0) {
		ar_snoc->xo_cal_supported = true;
		ath10k_dbg(ar, ATH10K_DBG_SNOC, "xo cal data %x\n",
			   ar_snoc->xo_cal_data);
	}
	ret = 0;

out:
	return ret;
}

static void ath10k_snoc_quirks_init(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct device *dev = &ar_snoc->dev->dev;

	if (of_property_read_bool(dev->of_node, "qcom,snoc-host-cap-8bit-quirk"))
		set_bit(ATH10K_SNOC_FLAG_8BIT_HOST_CAP_QUIRK, &ar_snoc->flags);
}

int ath10k_snoc_fw_indication(struct ath10k *ar, u64 type)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_bus_params bus_params = {};
	int ret;

	if (test_bit(ATH10K_SNOC_FLAG_UNREGISTERING, &ar_snoc->flags))
		return 0;

	switch (type) {
	case ATH10K_QMI_EVENT_FW_READY_IND:
		if (test_bit(ATH10K_SNOC_FLAG_REGISTERED, &ar_snoc->flags)) {
			queue_work(ar->workqueue, &ar->restart_work);
			break;
		}

		bus_params.dev_type = ATH10K_DEV_TYPE_LL;
		bus_params.chip_id = ar_snoc->target_info.soc_version;
		ret = ath10k_core_register(ar, &bus_params);
		if (ret) {
			ath10k_err(ar, "Failed to register driver core: %d\n",
				   ret);
			return ret;
		}
		set_bit(ATH10K_SNOC_FLAG_REGISTERED, &ar_snoc->flags);
		break;
	case ATH10K_QMI_EVENT_FW_DOWN_IND:
		set_bit(ATH10K_SNOC_FLAG_RECOVERY, &ar_snoc->flags);
		set_bit(ATH10K_FLAG_CRASH_FLUSH, &ar->dev_flags);
		break;
	default:
		ath10k_err(ar, "invalid fw indication: %llx\n", type);
		return -EINVAL;
	}

	return 0;
}

static int ath10k_snoc_setup_resource(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_snoc_pipe *pipe;
	int i, ret;

	timer_setup(&ar_snoc->rx_post_retry, ath10k_snoc_rx_replenish_retry, 0);
	spin_lock_init(&ce->ce_lock);
	for (i = 0; i < CE_COUNT; i++) {
		pipe = &ar_snoc->pipe_info[i];
		pipe->ce_hdl = &ce->ce_states[i];
		pipe->pipe_num = i;
		pipe->hif_ce_state = ar;

		ret = ath10k_ce_alloc_pipe(ar, i, &host_ce_config_wlan[i]);
		if (ret) {
			ath10k_err(ar, "failed to allocate copy engine pipe %d: %d\n",
				   i, ret);
			return ret;
		}

		pipe->buf_sz = host_ce_config_wlan[i].src_sz_max;
	}
	ath10k_snoc_init_napi(ar);

	return 0;
}

static void ath10k_snoc_release_resource(struct ath10k *ar)
{
	int i;

	netif_napi_del(&ar->napi);
	for (i = 0; i < CE_COUNT; i++)
		ath10k_ce_free_pipe(ar, i);
}

static int ath10k_hw_power_on(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "soc power on\n");

	ret = regulator_bulk_enable(ar_snoc->num_vregs, ar_snoc->vregs);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(ar_snoc->num_clks, ar_snoc->clks);
	if (ret)
		goto vreg_off;

	return ret;

vreg_off:
	regulator_bulk_disable(ar_snoc->num_vregs, ar_snoc->vregs);
	return ret;
}

static int ath10k_hw_power_off(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "soc power off\n");

	clk_bulk_disable_unprepare(ar_snoc->num_clks, ar_snoc->clks);

	return regulator_bulk_disable(ar_snoc->num_vregs, ar_snoc->vregs);
}

static void ath10k_msa_dump_memory(struct ath10k *ar,
				   struct ath10k_fw_crash_data *crash_data)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	const struct ath10k_hw_mem_layout *mem_layout;
	const struct ath10k_mem_region *current_region;
	struct ath10k_dump_ram_data_hdr *hdr;
	size_t buf_len;
	u8 *buf;

	if (!crash_data || !crash_data->ramdump_buf)
		return;

	mem_layout = ath10k_coredump_get_mem_layout(ar);
	if (!mem_layout)
		return;

	current_region = &mem_layout->region_table.regions[0];

	buf = crash_data->ramdump_buf;
	buf_len = crash_data->ramdump_buf_len;
	memset(buf, 0, buf_len);

	/* Reserve space for the header. */
	hdr = (void *)buf;
	buf += sizeof(*hdr);
	buf_len -= sizeof(*hdr);

	hdr->region_type = cpu_to_le32(current_region->type);
	hdr->start = cpu_to_le32((unsigned long)ar_snoc->qmi->msa_va);
	hdr->length = cpu_to_le32(ar_snoc->qmi->msa_mem_size);

	if (current_region->len < ar_snoc->qmi->msa_mem_size) {
		memcpy(buf, ar_snoc->qmi->msa_va, current_region->len);
		ath10k_warn(ar, "msa dump length is less than msa size %x, %x\n",
			    current_region->len, ar_snoc->qmi->msa_mem_size);
	} else {
		memcpy(buf, ar_snoc->qmi->msa_va, ar_snoc->qmi->msa_mem_size);
	}
}

void ath10k_snoc_fw_crashed_dump(struct ath10k *ar)
{
	struct ath10k_fw_crash_data *crash_data;
	char guid[UUID_STRING_LEN + 1];

	mutex_lock(&ar->dump_mutex);

	spin_lock_bh(&ar->data_lock);
	ar->stats.fw_crash_counter++;
	spin_unlock_bh(&ar->data_lock);

	crash_data = ath10k_coredump_new(ar);

	if (crash_data)
		scnprintf(guid, sizeof(guid), "%pUl", &crash_data->guid);
	else
		scnprintf(guid, sizeof(guid), "n/a");

	ath10k_err(ar, "firmware crashed! (guid %s)\n", guid);
	ath10k_print_driver_info(ar);
	ath10k_msa_dump_memory(ar, crash_data);
	mutex_unlock(&ar->dump_mutex);
}

static const struct of_device_id ath10k_snoc_dt_match[] = {
	{ .compatible = "qcom,wcn3990-wifi",
	 .data = &drv_priv,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ath10k_snoc_dt_match);

static int ath10k_snoc_probe(struct platform_device *pdev)
{
	const struct ath10k_snoc_drv_priv *drv_data;
	const struct of_device_id *of_id;
	struct ath10k_snoc *ar_snoc;
	struct device *dev;
	struct ath10k *ar;
	u32 msa_size;
	int ret;
	u32 i;

	of_id = of_match_device(ath10k_snoc_dt_match, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "failed to find matching device tree id\n");
		return -EINVAL;
	}

	drv_data = of_id->data;
	dev = &pdev->dev;

	ret = dma_set_mask_and_coherent(dev, drv_data->dma_mask);
	if (ret) {
		dev_err(dev, "failed to set dma mask: %d", ret);
		return ret;
	}

	ar = ath10k_core_create(sizeof(*ar_snoc), dev, ATH10K_BUS_SNOC,
				drv_data->hw_rev, &ath10k_snoc_hif_ops);
	if (!ar) {
		dev_err(dev, "failed to allocate core\n");
		return -ENOMEM;
	}

	ar_snoc = ath10k_snoc_priv(ar);
	ar_snoc->dev = pdev;
	platform_set_drvdata(pdev, ar);
	ar_snoc->ar = ar;
	ar_snoc->ce.bus_ops = &ath10k_snoc_bus_ops;
	ar->ce_priv = &ar_snoc->ce;
	msa_size = drv_data->msa_size;

	ath10k_snoc_quirks_init(ar);

	ret = ath10k_snoc_resource_init(ar);
	if (ret) {
		ath10k_warn(ar, "failed to initialize resource: %d\n", ret);
		goto err_core_destroy;
	}

	ret = ath10k_snoc_setup_resource(ar);
	if (ret) {
		ath10k_warn(ar, "failed to setup resource: %d\n", ret);
		goto err_core_destroy;
	}
	ret = ath10k_snoc_request_irq(ar);
	if (ret) {
		ath10k_warn(ar, "failed to request irqs: %d\n", ret);
		goto err_release_resource;
	}

	ar_snoc->num_vregs = ARRAY_SIZE(ath10k_regulators);
	ar_snoc->vregs = devm_kcalloc(&pdev->dev, ar_snoc->num_vregs,
				      sizeof(*ar_snoc->vregs), GFP_KERNEL);
	if (!ar_snoc->vregs) {
		ret = -ENOMEM;
		goto err_free_irq;
	}
	for (i = 0; i < ar_snoc->num_vregs; i++)
		ar_snoc->vregs[i].supply = ath10k_regulators[i];

	ret = devm_regulator_bulk_get(&pdev->dev, ar_snoc->num_vregs,
				      ar_snoc->vregs);
	if (ret < 0)
		goto err_free_irq;

	ar_snoc->num_clks = ARRAY_SIZE(ath10k_clocks);
	ar_snoc->clks = devm_kcalloc(&pdev->dev, ar_snoc->num_clks,
				     sizeof(*ar_snoc->clks), GFP_KERNEL);
	if (!ar_snoc->clks) {
		ret = -ENOMEM;
		goto err_free_irq;
	}

	for (i = 0; i < ar_snoc->num_clks; i++)
		ar_snoc->clks[i].id = ath10k_clocks[i];

	ret = devm_clk_bulk_get_optional(&pdev->dev, ar_snoc->num_clks,
					 ar_snoc->clks);
	if (ret)
		goto err_free_irq;

	ret = ath10k_hw_power_on(ar);
	if (ret) {
		ath10k_err(ar, "failed to power on device: %d\n", ret);
		goto err_free_irq;
	}

	ret = ath10k_qmi_init(ar, msa_size);
	if (ret) {
		ath10k_warn(ar, "failed to register wlfw qmi client: %d\n", ret);
		goto err_power_off;
	}

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc probe\n");

	return 0;

err_power_off:
	ath10k_hw_power_off(ar);

err_free_irq:
	ath10k_snoc_free_irq(ar);

err_release_resource:
	ath10k_snoc_release_resource(ar);

err_core_destroy:
	ath10k_core_destroy(ar);

	return ret;
}

static int ath10k_snoc_remove(struct platform_device *pdev)
{
	struct ath10k *ar = platform_get_drvdata(pdev);
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc remove\n");

	reinit_completion(&ar->driver_recovery);

	if (test_bit(ATH10K_SNOC_FLAG_RECOVERY, &ar_snoc->flags))
		wait_for_completion_timeout(&ar->driver_recovery, 3 * HZ);

	set_bit(ATH10K_SNOC_FLAG_UNREGISTERING, &ar_snoc->flags);

	ath10k_core_unregister(ar);
	ath10k_hw_power_off(ar);
	ath10k_snoc_free_irq(ar);
	ath10k_snoc_release_resource(ar);
	ath10k_qmi_deinit(ar);
	ath10k_core_destroy(ar);

	return 0;
}

static struct platform_driver ath10k_snoc_driver = {
	.probe  = ath10k_snoc_probe,
	.remove = ath10k_snoc_remove,
	.driver = {
		.name   = "ath10k_snoc",
		.of_match_table = ath10k_snoc_dt_match,
	},
};
module_platform_driver(ath10k_snoc_driver);

MODULE_AUTHOR("Qualcomm");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Driver support for Atheros WCN3990 SNOC devices");
