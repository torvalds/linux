// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/units.h>

#include <net/mac80211.h>

#include <asm/div64.h>

#define DRV_NAME        "iwl4965"

#include "common.h"
#include "4965.h"

/******************************************************************************
 *
 * module boiler plate
 *
 ******************************************************************************/

/*
 * module name, copyright, version, etc.
 */
#define DRV_DESCRIPTION	"Intel(R) Wireless WiFi 4965 driver for Linux"

#ifdef CONFIG_IWLEGACY_DEBUG
#define VD "d"
#else
#define VD
#endif

#define DRV_VERSION     IWLWIFI_VERSION VD

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_ALIAS("iwl4965");

void
il4965_check_abort_status(struct il_priv *il, u8 frame_count, u32 status)
{
	if (frame_count == 1 && status == TX_STATUS_FAIL_RFKILL_FLUSH) {
		IL_ERR("Tx flush command to flush out all frames\n");
		if (!test_bit(S_EXIT_PENDING, &il->status))
			queue_work(il->workqueue, &il->tx_flush);
	}
}

/*
 * EEPROM
 */
struct il_mod_params il4965_mod_params = {
	.restart_fw = 1,
	/* the rest are 0 by default */
};

void
il4965_rx_queue_reset(struct il_priv *il, struct il_rx_queue *rxq)
{
	unsigned long flags;
	int i;
	spin_lock_irqsave(&rxq->lock, flags);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);
	/* Fill the rx_used queue with _all_ of the Rx buffers */
	for (i = 0; i < RX_FREE_BUFFERS + RX_QUEUE_SIZE; i++) {
		/* In the reset function, these buffers may have been allocated
		 * to an SKB, so we need to unmap and free potential storage */
		if (rxq->pool[i].page != NULL) {
			dma_unmap_page(&il->pci_dev->dev,
				       rxq->pool[i].page_dma,
				       PAGE_SIZE << il->hw_params.rx_page_order,
				       DMA_FROM_DEVICE);
			__il_free_pages(il, rxq->pool[i].page);
			rxq->pool[i].page = NULL;
		}
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);
	}

	for (i = 0; i < RX_QUEUE_SIZE; i++)
		rxq->queue[i] = NULL;

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->write_actual = 0;
	rxq->free_count = 0;
	spin_unlock_irqrestore(&rxq->lock, flags);
}

int
il4965_rx_init(struct il_priv *il, struct il_rx_queue *rxq)
{
	u32 rb_size;
	const u32 rfdnlog = RX_QUEUE_SIZE_LOG;	/* 256 RBDs */
	u32 rb_timeout = 0;

	if (il->cfg->mod_params->amsdu_size_8K)
		rb_size = FH49_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_8K;
	else
		rb_size = FH49_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K;

	/* Stop Rx DMA */
	il_wr(il, FH49_MEM_RCSR_CHNL0_CONFIG_REG, 0);

	/* Reset driver's Rx queue write idx */
	il_wr(il, FH49_RSCSR_CHNL0_RBDCB_WPTR_REG, 0);

	/* Tell device where to find RBD circular buffer in DRAM */
	il_wr(il, FH49_RSCSR_CHNL0_RBDCB_BASE_REG, (u32) (rxq->bd_dma >> 8));

	/* Tell device where in DRAM to update its Rx status */
	il_wr(il, FH49_RSCSR_CHNL0_STTS_WPTR_REG, rxq->rb_stts_dma >> 4);

	/* Enable Rx DMA
	 * Direct rx interrupts to hosts
	 * Rx buffer size 4 or 8k
	 * RB timeout 0x10
	 * 256 RBDs
	 */
	il_wr(il, FH49_MEM_RCSR_CHNL0_CONFIG_REG,
	      FH49_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL |
	      FH49_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL |
	      FH49_RCSR_CHNL0_RX_CONFIG_SINGLE_FRAME_MSK |
	      rb_size |
	      (rb_timeout << FH49_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS) |
	      (rfdnlog << FH49_RCSR_RX_CONFIG_RBDCB_SIZE_POS));

	/* Set interrupt coalescing timer to default (2048 usecs) */
	il_write8(il, CSR_INT_COALESCING, IL_HOST_INT_TIMEOUT_DEF);

	return 0;
}

static void
il4965_set_pwr_vmain(struct il_priv *il)
{
/*
 * (for documentation purposes)
 * to set power to V_AUX, do:

		if (pci_pme_capable(il->pci_dev, PCI_D3cold))
			il_set_bits_mask_prph(il, APMG_PS_CTRL_REG,
					       APMG_PS_CTRL_VAL_PWR_SRC_VAUX,
					       ~APMG_PS_CTRL_MSK_PWR_SRC);
 */

	il_set_bits_mask_prph(il, APMG_PS_CTRL_REG,
			      APMG_PS_CTRL_VAL_PWR_SRC_VMAIN,
			      ~APMG_PS_CTRL_MSK_PWR_SRC);
}

int
il4965_hw_nic_init(struct il_priv *il)
{
	unsigned long flags;
	struct il_rx_queue *rxq = &il->rxq;
	int ret;

	spin_lock_irqsave(&il->lock, flags);
	il_apm_init(il);
	/* Set interrupt coalescing calibration timer to default (512 usecs) */
	il_write8(il, CSR_INT_COALESCING, IL_HOST_INT_CALIB_TIMEOUT_DEF);
	spin_unlock_irqrestore(&il->lock, flags);

	il4965_set_pwr_vmain(il);
	il4965_nic_config(il);

	/* Allocate the RX queue, or reset if it is already allocated */
	if (!rxq->bd) {
		ret = il_rx_queue_alloc(il);
		if (ret) {
			IL_ERR("Unable to initialize Rx queue\n");
			return -ENOMEM;
		}
	} else
		il4965_rx_queue_reset(il, rxq);

	il4965_rx_replenish(il);

	il4965_rx_init(il, rxq);

	spin_lock_irqsave(&il->lock, flags);

	rxq->need_update = 1;
	il_rx_queue_update_write_ptr(il, rxq);

	spin_unlock_irqrestore(&il->lock, flags);

	/* Allocate or reset and init all Tx and Command queues */
	if (!il->txq) {
		ret = il4965_txq_ctx_alloc(il);
		if (ret)
			return ret;
	} else
		il4965_txq_ctx_reset(il);

	set_bit(S_INIT, &il->status);

	return 0;
}

/*
 * il4965_dma_addr2rbd_ptr - convert a DMA address to a uCode read buffer ptr
 */
static inline __le32
il4965_dma_addr2rbd_ptr(struct il_priv *il, dma_addr_t dma_addr)
{
	return cpu_to_le32((u32) (dma_addr >> 8));
}

/*
 * il4965_rx_queue_restock - refill RX queue from pre-allocated pool
 *
 * If there are slots in the RX queue that need to be restocked,
 * and we have free pre-allocated buffers, fill the ranks as much
 * as we can, pulling from rx_free.
 *
 * This moves the 'write' idx forward to catch up with 'processed', and
 * also updates the memory address in the firmware to reference the new
 * target buffer.
 */
void
il4965_rx_queue_restock(struct il_priv *il)
{
	struct il_rx_queue *rxq = &il->rxq;
	struct list_head *element;
	struct il_rx_buf *rxb;
	unsigned long flags;

	spin_lock_irqsave(&rxq->lock, flags);
	while (il_rx_queue_space(rxq) > 0 && rxq->free_count) {
		/* The overwritten rxb must be a used one */
		rxb = rxq->queue[rxq->write];
		BUG_ON(rxb && rxb->page);

		/* Get next free Rx buffer, remove from free list */
		element = rxq->rx_free.next;
		rxb = list_entry(element, struct il_rx_buf, list);
		list_del(element);

		/* Point to Rx buffer via next RBD in circular buffer */
		rxq->bd[rxq->write] =
		    il4965_dma_addr2rbd_ptr(il, rxb->page_dma);
		rxq->queue[rxq->write] = rxb;
		rxq->write = (rxq->write + 1) & RX_QUEUE_MASK;
		rxq->free_count--;
	}
	spin_unlock_irqrestore(&rxq->lock, flags);
	/* If the pre-allocated buffer pool is dropping low, schedule to
	 * refill it */
	if (rxq->free_count <= RX_LOW_WATERMARK)
		queue_work(il->workqueue, &il->rx_replenish);

	/* If we've added more space for the firmware to place data, tell it.
	 * Increment device's write pointer in multiples of 8. */
	if (rxq->write_actual != (rxq->write & ~0x7)) {
		spin_lock_irqsave(&rxq->lock, flags);
		rxq->need_update = 1;
		spin_unlock_irqrestore(&rxq->lock, flags);
		il_rx_queue_update_write_ptr(il, rxq);
	}
}

/*
 * il4965_rx_replenish - Move all used packet from rx_used to rx_free
 *
 * When moving to rx_free an SKB is allocated for the slot.
 *
 * Also restock the Rx queue via il_rx_queue_restock.
 * This is called as a scheduled work item (except for during initialization)
 */
static void
il4965_rx_allocate(struct il_priv *il, gfp_t priority)
{
	struct il_rx_queue *rxq = &il->rxq;
	struct list_head *element;
	struct il_rx_buf *rxb;
	struct page *page;
	dma_addr_t page_dma;
	unsigned long flags;
	gfp_t gfp_mask = priority;

	while (1) {
		spin_lock_irqsave(&rxq->lock, flags);
		if (list_empty(&rxq->rx_used)) {
			spin_unlock_irqrestore(&rxq->lock, flags);
			return;
		}
		spin_unlock_irqrestore(&rxq->lock, flags);

		if (rxq->free_count > RX_LOW_WATERMARK)
			gfp_mask |= __GFP_NOWARN;

		if (il->hw_params.rx_page_order > 0)
			gfp_mask |= __GFP_COMP;

		/* Alloc a new receive buffer */
		page = alloc_pages(gfp_mask, il->hw_params.rx_page_order);
		if (!page) {
			if (net_ratelimit())
				D_INFO("alloc_pages failed, " "order: %d\n",
				       il->hw_params.rx_page_order);

			if (rxq->free_count <= RX_LOW_WATERMARK &&
			    net_ratelimit())
				IL_ERR("Failed to alloc_pages with %s. "
				       "Only %u free buffers remaining.\n",
				       priority ==
				       GFP_ATOMIC ? "GFP_ATOMIC" : "GFP_KERNEL",
				       rxq->free_count);
			/* We don't reschedule replenish work here -- we will
			 * call the restock method and if it still needs
			 * more buffers it will schedule replenish */
			return;
		}

		/* Get physical address of the RB */
		page_dma = dma_map_page(&il->pci_dev->dev, page, 0,
					PAGE_SIZE << il->hw_params.rx_page_order,
					DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(&il->pci_dev->dev, page_dma))) {
			__free_pages(page, il->hw_params.rx_page_order);
			break;
		}

		spin_lock_irqsave(&rxq->lock, flags);

		if (list_empty(&rxq->rx_used)) {
			spin_unlock_irqrestore(&rxq->lock, flags);
			dma_unmap_page(&il->pci_dev->dev, page_dma,
				       PAGE_SIZE << il->hw_params.rx_page_order,
				       DMA_FROM_DEVICE);
			__free_pages(page, il->hw_params.rx_page_order);
			return;
		}

		element = rxq->rx_used.next;
		rxb = list_entry(element, struct il_rx_buf, list);
		list_del(element);

		BUG_ON(rxb->page);

		rxb->page = page;
		rxb->page_dma = page_dma;
		list_add_tail(&rxb->list, &rxq->rx_free);
		rxq->free_count++;
		il->alloc_rxb_page++;

		spin_unlock_irqrestore(&rxq->lock, flags);
	}
}

void
il4965_rx_replenish(struct il_priv *il)
{
	unsigned long flags;

	il4965_rx_allocate(il, GFP_KERNEL);

	spin_lock_irqsave(&il->lock, flags);
	il4965_rx_queue_restock(il);
	spin_unlock_irqrestore(&il->lock, flags);
}

void
il4965_rx_replenish_now(struct il_priv *il)
{
	il4965_rx_allocate(il, GFP_ATOMIC);

	il4965_rx_queue_restock(il);
}

/* Assumes that the skb field of the buffers in 'pool' is kept accurate.
 * If an SKB has been detached, the POOL needs to have its SKB set to NULL
 * This free routine walks the list of POOL entries and if SKB is set to
 * non NULL it is unmapped and freed
 */
void
il4965_rx_queue_free(struct il_priv *il, struct il_rx_queue *rxq)
{
	int i;
	for (i = 0; i < RX_QUEUE_SIZE + RX_FREE_BUFFERS; i++) {
		if (rxq->pool[i].page != NULL) {
			dma_unmap_page(&il->pci_dev->dev,
				       rxq->pool[i].page_dma,
				       PAGE_SIZE << il->hw_params.rx_page_order,
				       DMA_FROM_DEVICE);
			__il_free_pages(il, rxq->pool[i].page);
			rxq->pool[i].page = NULL;
		}
	}

	dma_free_coherent(&il->pci_dev->dev, 4 * RX_QUEUE_SIZE, rxq->bd,
			  rxq->bd_dma);
	dma_free_coherent(&il->pci_dev->dev, sizeof(struct il_rb_status),
			  rxq->rb_stts, rxq->rb_stts_dma);
	rxq->bd = NULL;
	rxq->rb_stts = NULL;
}

int
il4965_rxq_stop(struct il_priv *il)
{
	int ret;

	_il_wr(il, FH49_MEM_RCSR_CHNL0_CONFIG_REG, 0);
	ret = _il_poll_bit(il, FH49_MEM_RSSR_RX_STATUS_REG,
			   FH49_RSSR_CHNL0_RX_STATUS_CHNL_IDLE,
			   FH49_RSSR_CHNL0_RX_STATUS_CHNL_IDLE,
			   1000);
	if (ret < 0)
		IL_ERR("Can't stop Rx DMA.\n");

	return 0;
}

int
il4965_hwrate_to_mac80211_idx(u32 rate_n_flags, enum nl80211_band band)
{
	int idx = 0;
	int band_offset = 0;

	/* HT rate format: mac80211 wants an MCS number, which is just LSB */
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		idx = (rate_n_flags & 0xff);
		return idx;
		/* Legacy rate format, search for match in table */
	} else {
		if (band == NL80211_BAND_5GHZ)
			band_offset = IL_FIRST_OFDM_RATE;
		for (idx = band_offset; idx < RATE_COUNT_LEGACY; idx++)
			if (il_rates[idx].plcp == (rate_n_flags & 0xFF))
				return idx - band_offset;
	}

	return -1;
}

static int
il4965_calc_rssi(struct il_priv *il, struct il_rx_phy_res *rx_resp)
{
	/* data from PHY/DSP regarding signal strength, etc.,
	 *   contents are always there, not configurable by host.  */
	struct il4965_rx_non_cfg_phy *ncphy =
	    (struct il4965_rx_non_cfg_phy *)rx_resp->non_cfg_phy_buf;
	u32 agc =
	    (le16_to_cpu(ncphy->agc_info) & IL49_AGC_DB_MASK) >>
	    IL49_AGC_DB_POS;

	u32 valid_antennae =
	    (le16_to_cpu(rx_resp->phy_flags) & IL49_RX_PHY_FLAGS_ANTENNAE_MASK)
	    >> IL49_RX_PHY_FLAGS_ANTENNAE_OFFSET;
	u8 max_rssi = 0;
	u32 i;

	/* Find max rssi among 3 possible receivers.
	 * These values are measured by the digital signal processor (DSP).
	 * They should stay fairly constant even as the signal strength varies,
	 *   if the radio's automatic gain control (AGC) is working right.
	 * AGC value (see below) will provide the "interesting" info. */
	for (i = 0; i < 3; i++)
		if (valid_antennae & (1 << i))
			max_rssi = max(ncphy->rssi_info[i << 1], max_rssi);

	D_STATS("Rssi In A %d B %d C %d Max %d AGC dB %d\n",
		ncphy->rssi_info[0], ncphy->rssi_info[2], ncphy->rssi_info[4],
		max_rssi, agc);

	/* dBm = max_rssi dB - agc dB - constant.
	 * Higher AGC (higher radio gain) means lower signal. */
	return max_rssi - agc - IL4965_RSSI_OFFSET;
}

static u32
il4965_translate_rx_status(struct il_priv *il, u32 decrypt_in)
{
	u32 decrypt_out = 0;

	if ((decrypt_in & RX_RES_STATUS_STATION_FOUND) ==
	    RX_RES_STATUS_STATION_FOUND)
		decrypt_out |=
		    (RX_RES_STATUS_STATION_FOUND |
		     RX_RES_STATUS_NO_STATION_INFO_MISMATCH);

	decrypt_out |= (decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK);

	/* packet was not encrypted */
	if ((decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK) ==
	    RX_RES_STATUS_SEC_TYPE_NONE)
		return decrypt_out;

	/* packet was encrypted with unknown alg */
	if ((decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK) ==
	    RX_RES_STATUS_SEC_TYPE_ERR)
		return decrypt_out;

	/* decryption was not done in HW */
	if ((decrypt_in & RX_MPDU_RES_STATUS_DEC_DONE_MSK) !=
	    RX_MPDU_RES_STATUS_DEC_DONE_MSK)
		return decrypt_out;

	switch (decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK) {

	case RX_RES_STATUS_SEC_TYPE_CCMP:
		/* alg is CCM: check MIC only */
		if (!(decrypt_in & RX_MPDU_RES_STATUS_MIC_OK))
			/* Bad MIC */
			decrypt_out |= RX_RES_STATUS_BAD_ICV_MIC;
		else
			decrypt_out |= RX_RES_STATUS_DECRYPT_OK;

		break;

	case RX_RES_STATUS_SEC_TYPE_TKIP:
		if (!(decrypt_in & RX_MPDU_RES_STATUS_TTAK_OK)) {
			/* Bad TTAK */
			decrypt_out |= RX_RES_STATUS_BAD_KEY_TTAK;
			break;
		}
		fallthrough;	/* if TTAK OK */
	default:
		if (!(decrypt_in & RX_MPDU_RES_STATUS_ICV_OK))
			decrypt_out |= RX_RES_STATUS_BAD_ICV_MIC;
		else
			decrypt_out |= RX_RES_STATUS_DECRYPT_OK;
		break;
	}

	D_RX("decrypt_in:0x%x  decrypt_out = 0x%x\n", decrypt_in, decrypt_out);

	return decrypt_out;
}

#define SMALL_PACKET_SIZE 256

static void
il4965_pass_packet_to_mac80211(struct il_priv *il, struct ieee80211_hdr *hdr,
			       u32 len, u32 ampdu_status, struct il_rx_buf *rxb,
			       struct ieee80211_rx_status *stats)
{
	struct sk_buff *skb;
	__le16 fc = hdr->frame_control;

	/* We only process data packets if the interface is open */
	if (unlikely(!il->is_open)) {
		D_DROP("Dropping packet while interface is not open.\n");
		return;
	}

	if (unlikely(test_bit(IL_STOP_REASON_PASSIVE, &il->stop_reason))) {
		il_wake_queues_by_reason(il, IL_STOP_REASON_PASSIVE);
		D_INFO("Woke queues - frame received on passive channel\n");
	}

	/* In case of HW accelerated crypto and bad decryption, drop */
	if (!il->cfg->mod_params->sw_crypto &&
	    il_set_decrypted_flag(il, hdr, ampdu_status, stats))
		return;

	skb = dev_alloc_skb(SMALL_PACKET_SIZE);
	if (!skb) {
		IL_ERR("dev_alloc_skb failed\n");
		return;
	}

	if (len <= SMALL_PACKET_SIZE) {
		skb_put_data(skb, hdr, len);
	} else {
		skb_add_rx_frag(skb, 0, rxb->page, (void *)hdr - rxb_addr(rxb),
				len, PAGE_SIZE << il->hw_params.rx_page_order);
		il->alloc_rxb_page--;
		rxb->page = NULL;
	}

	il_update_stats(il, false, fc, len);
	memcpy(IEEE80211_SKB_RXCB(skb), stats, sizeof(*stats));

	ieee80211_rx(il->hw, skb);
}

/* Called for N_RX (legacy ABG frames), or
 * N_RX_MPDU (HT high-throughput N frames). */
static void
il4965_hdl_rx(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct ieee80211_hdr *header;
	struct ieee80211_rx_status rx_status = {};
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_rx_phy_res *phy_res;
	__le32 rx_pkt_status;
	struct il_rx_mpdu_res_start *amsdu;
	u32 len;
	u32 ampdu_status;
	u32 rate_n_flags;

	/**
	 * N_RX and N_RX_MPDU are handled differently.
	 *	N_RX: physical layer info is in this buffer
	 *	N_RX_MPDU: physical layer info was sent in separate
	 *		command and cached in il->last_phy_res
	 *
	 * Here we set up local variables depending on which command is
	 * received.
	 */
	if (pkt->hdr.cmd == N_RX) {
		phy_res = (struct il_rx_phy_res *)pkt->u.raw;
		header =
		    (struct ieee80211_hdr *)(pkt->u.raw + sizeof(*phy_res) +
					     phy_res->cfg_phy_cnt);

		len = le16_to_cpu(phy_res->byte_count);
		rx_pkt_status =
		    *(__le32 *) (pkt->u.raw + sizeof(*phy_res) +
				 phy_res->cfg_phy_cnt + len);
		ampdu_status = le32_to_cpu(rx_pkt_status);
	} else {
		if (!il->_4965.last_phy_res_valid) {
			IL_ERR("MPDU frame without cached PHY data\n");
			return;
		}
		phy_res = &il->_4965.last_phy_res;
		amsdu = (struct il_rx_mpdu_res_start *)pkt->u.raw;
		header = (struct ieee80211_hdr *)(pkt->u.raw + sizeof(*amsdu));
		len = le16_to_cpu(amsdu->byte_count);
		rx_pkt_status = *(__le32 *) (pkt->u.raw + sizeof(*amsdu) + len);
		ampdu_status =
		    il4965_translate_rx_status(il, le32_to_cpu(rx_pkt_status));
	}

	if ((unlikely(phy_res->cfg_phy_cnt > 20))) {
		D_DROP("dsp size out of range [0,20]: %d\n",
		       phy_res->cfg_phy_cnt);
		return;
	}

	if (!(rx_pkt_status & RX_RES_STATUS_NO_CRC32_ERROR) ||
	    !(rx_pkt_status & RX_RES_STATUS_NO_RXE_OVERFLOW)) {
		D_RX("Bad CRC or FIFO: 0x%08X.\n", le32_to_cpu(rx_pkt_status));
		return;
	}

	/* This will be used in several places later */
	rate_n_flags = le32_to_cpu(phy_res->rate_n_flags);

	/* rx_status carries information about the packet to mac80211 */
	rx_status.mactime = le64_to_cpu(phy_res->timestamp);
	rx_status.band =
	    (phy_res->
	     phy_flags & RX_RES_PHY_FLAGS_BAND_24_MSK) ? NL80211_BAND_2GHZ :
	    NL80211_BAND_5GHZ;
	rx_status.freq =
	    ieee80211_channel_to_frequency(le16_to_cpu(phy_res->channel),
					   rx_status.band);
	rx_status.rate_idx =
	    il4965_hwrate_to_mac80211_idx(rate_n_flags, rx_status.band);
	rx_status.flag = 0;

	/* TSF isn't reliable. In order to allow smooth user experience,
	 * this W/A doesn't propagate it to the mac80211 */
	/*rx_status.flag |= RX_FLAG_MACTIME_START; */

	il->ucode_beacon_time = le32_to_cpu(phy_res->beacon_time_stamp);

	/* Find max signal strength (dBm) among 3 antenna/receiver chains */
	rx_status.signal = il4965_calc_rssi(il, phy_res);

	D_STATS("Rssi %d, TSF %llu\n", rx_status.signal,
		(unsigned long long)rx_status.mactime);

	/*
	 * "antenna number"
	 *
	 * It seems that the antenna field in the phy flags value
	 * is actually a bit field. This is undefined by radiotap,
	 * it wants an actual antenna number but I always get "7"
	 * for most legacy frames I receive indicating that the
	 * same frame was received on all three RX chains.
	 *
	 * I think this field should be removed in favor of a
	 * new 802.11n radiotap field "RX chains" that is defined
	 * as a bitmask.
	 */
	rx_status.antenna =
	    (le16_to_cpu(phy_res->phy_flags) & RX_RES_PHY_FLAGS_ANTENNA_MSK) >>
	    RX_RES_PHY_FLAGS_ANTENNA_POS;

	/* set the preamble flag if appropriate */
	if (phy_res->phy_flags & RX_RES_PHY_FLAGS_SHORT_PREAMBLE_MSK)
		rx_status.enc_flags |= RX_ENC_FLAG_SHORTPRE;

	/* Set up the HT phy flags */
	if (rate_n_flags & RATE_MCS_HT_MSK)
		rx_status.encoding = RX_ENC_HT;
	if (rate_n_flags & RATE_MCS_HT40_MSK)
		rx_status.bw = RATE_INFO_BW_40;
	else
		rx_status.bw = RATE_INFO_BW_20;
	if (rate_n_flags & RATE_MCS_SGI_MSK)
		rx_status.enc_flags |= RX_ENC_FLAG_SHORT_GI;

	if (phy_res->phy_flags & RX_RES_PHY_FLAGS_AGG_MSK) {
		/* We know which subframes of an A-MPDU belong
		 * together since we get a single PHY response
		 * from the firmware for all of them.
		 */

		rx_status.flag |= RX_FLAG_AMPDU_DETAILS;
		rx_status.ampdu_reference = il->_4965.ampdu_ref;
	}

	il4965_pass_packet_to_mac80211(il, header, len, ampdu_status, rxb,
				       &rx_status);
}

/* Cache phy data (Rx signal strength, etc) for HT frame (N_RX_PHY).
 * This will be used later in il_hdl_rx() for N_RX_MPDU. */
static void
il4965_hdl_rx_phy(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	il->_4965.last_phy_res_valid = true;
	il->_4965.ampdu_ref++;
	memcpy(&il->_4965.last_phy_res, pkt->u.raw,
	       sizeof(struct il_rx_phy_res));
}

static int
il4965_get_channels_for_scan(struct il_priv *il, struct ieee80211_vif *vif,
			     enum nl80211_band band, u8 is_active,
			     u8 n_probes, struct il_scan_channel *scan_ch)
{
	struct ieee80211_channel *chan;
	const struct ieee80211_supported_band *sband;
	const struct il_channel_info *ch_info;
	u16 passive_dwell = 0;
	u16 active_dwell = 0;
	int added, i;
	u16 channel;

	sband = il_get_hw_mode(il, band);
	if (!sband)
		return 0;

	active_dwell = il_get_active_dwell_time(il, band, n_probes);
	passive_dwell = il_get_passive_dwell_time(il, band, vif);

	if (passive_dwell <= active_dwell)
		passive_dwell = active_dwell + 1;

	for (i = 0, added = 0; i < il->scan_request->n_channels; i++) {
		chan = il->scan_request->channels[i];

		if (chan->band != band)
			continue;

		channel = chan->hw_value;
		scan_ch->channel = cpu_to_le16(channel);

		ch_info = il_get_channel_info(il, band, channel);
		if (!il_is_channel_valid(ch_info)) {
			D_SCAN("Channel %d is INVALID for this band.\n",
			       channel);
			continue;
		}

		if (!is_active || il_is_channel_passive(ch_info) ||
		    (chan->flags & IEEE80211_CHAN_NO_IR))
			scan_ch->type = SCAN_CHANNEL_TYPE_PASSIVE;
		else
			scan_ch->type = SCAN_CHANNEL_TYPE_ACTIVE;

		if (n_probes)
			scan_ch->type |= IL_SCAN_PROBE_MASK(n_probes);

		scan_ch->active_dwell = cpu_to_le16(active_dwell);
		scan_ch->passive_dwell = cpu_to_le16(passive_dwell);

		/* Set txpower levels to defaults */
		scan_ch->dsp_atten = 110;

		/* NOTE: if we were doing 6Mb OFDM for scans we'd use
		 * power level:
		 * scan_ch->tx_gain = ((1 << 5) | (2 << 3)) | 3;
		 */
		if (band == NL80211_BAND_5GHZ)
			scan_ch->tx_gain = ((1 << 5) | (3 << 3)) | 3;
		else
			scan_ch->tx_gain = ((1 << 5) | (5 << 3));

		D_SCAN("Scanning ch=%d prob=0x%X [%s %d]\n", channel,
		       le32_to_cpu(scan_ch->type),
		       (scan_ch->
			type & SCAN_CHANNEL_TYPE_ACTIVE) ? "ACTIVE" : "PASSIVE",
		       (scan_ch->
			type & SCAN_CHANNEL_TYPE_ACTIVE) ? active_dwell :
		       passive_dwell);

		scan_ch++;
		added++;
	}

	D_SCAN("total channels to scan %d\n", added);
	return added;
}

static void
il4965_toggle_tx_ant(struct il_priv *il, u8 *ant, u8 valid)
{
	int i;
	u8 ind = *ant;

	for (i = 0; i < RATE_ANT_NUM - 1; i++) {
		ind = (ind + 1) < RATE_ANT_NUM ? ind + 1 : 0;
		if (valid & BIT(ind)) {
			*ant = ind;
			return;
		}
	}
}

int
il4965_request_scan(struct il_priv *il, struct ieee80211_vif *vif)
{
	struct il_host_cmd cmd = {
		.id = C_SCAN,
		.len = sizeof(struct il_scan_cmd),
		.flags = CMD_SIZE_HUGE,
	};
	struct il_scan_cmd *scan;
	u32 rate_flags = 0;
	u16 cmd_len;
	u16 rx_chain = 0;
	enum nl80211_band band;
	u8 n_probes = 0;
	u8 rx_ant = il->hw_params.valid_rx_ant;
	u8 rate;
	bool is_active = false;
	int chan_mod;
	u8 active_chains;
	u8 scan_tx_antennas = il->hw_params.valid_tx_ant;
	int ret;

	lockdep_assert_held(&il->mutex);

	if (!il->scan_cmd) {
		il->scan_cmd =
		    kmalloc(sizeof(struct il_scan_cmd) + IL_MAX_SCAN_SIZE,
			    GFP_KERNEL);
		if (!il->scan_cmd) {
			D_SCAN("fail to allocate memory for scan\n");
			return -ENOMEM;
		}
	}
	scan = il->scan_cmd;
	memset(scan, 0, sizeof(struct il_scan_cmd) + IL_MAX_SCAN_SIZE);

	scan->quiet_plcp_th = IL_PLCP_QUIET_THRESH;
	scan->quiet_time = IL_ACTIVE_QUIET_TIME;

	if (il_is_any_associated(il)) {
		u16 interval;
		u32 extra;
		u32 suspend_time = 100;
		u32 scan_suspend_time = 100;

		D_INFO("Scanning while associated...\n");
		interval = vif->bss_conf.beacon_int;

		scan->suspend_time = 0;
		scan->max_out_time = cpu_to_le32(200 * 1024);
		if (!interval)
			interval = suspend_time;

		extra = (suspend_time / interval) << 22;
		scan_suspend_time =
		    (extra | ((suspend_time % interval) * 1024));
		scan->suspend_time = cpu_to_le32(scan_suspend_time);
		D_SCAN("suspend_time 0x%X beacon interval %d\n",
		       scan_suspend_time, interval);
	}

	if (il->scan_request->n_ssids) {
		int i, p = 0;
		D_SCAN("Kicking off active scan\n");
		for (i = 0; i < il->scan_request->n_ssids; i++) {
			/* always does wildcard anyway */
			if (!il->scan_request->ssids[i].ssid_len)
				continue;
			scan->direct_scan[p].id = WLAN_EID_SSID;
			scan->direct_scan[p].len =
			    il->scan_request->ssids[i].ssid_len;
			memcpy(scan->direct_scan[p].ssid,
			       il->scan_request->ssids[i].ssid,
			       il->scan_request->ssids[i].ssid_len);
			n_probes++;
			p++;
		}
		is_active = true;
	} else
		D_SCAN("Start passive scan.\n");

	scan->tx_cmd.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK;
	scan->tx_cmd.sta_id = il->hw_params.bcast_id;
	scan->tx_cmd.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;

	switch (il->scan_band) {
	case NL80211_BAND_2GHZ:
		scan->flags = RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK;
		chan_mod =
		    le32_to_cpu(il->active.flags & RXON_FLG_CHANNEL_MODE_MSK) >>
		    RXON_FLG_CHANNEL_MODE_POS;
		if (chan_mod == CHANNEL_MODE_PURE_40) {
			rate = RATE_6M_PLCP;
		} else {
			rate = RATE_1M_PLCP;
			rate_flags = RATE_MCS_CCK_MSK;
		}
		break;
	case NL80211_BAND_5GHZ:
		rate = RATE_6M_PLCP;
		break;
	default:
		IL_WARN("Invalid scan band\n");
		return -EIO;
	}

	/*
	 * If active scanning is requested but a certain channel is
	 * marked passive, we can do active scanning if we detect
	 * transmissions.
	 *
	 * There is an issue with some firmware versions that triggers
	 * a sysassert on a "good CRC threshold" of zero (== disabled),
	 * on a radar channel even though this means that we should NOT
	 * send probes.
	 *
	 * The "good CRC threshold" is the number of frames that we
	 * need to receive during our dwell time on a channel before
	 * sending out probes -- setting this to a huge value will
	 * mean we never reach it, but at the same time work around
	 * the aforementioned issue. Thus use IL_GOOD_CRC_TH_NEVER
	 * here instead of IL_GOOD_CRC_TH_DISABLED.
	 */
	scan->good_CRC_th =
	    is_active ? IL_GOOD_CRC_TH_DEFAULT : IL_GOOD_CRC_TH_NEVER;

	band = il->scan_band;

	if (il->cfg->scan_rx_antennas[band])
		rx_ant = il->cfg->scan_rx_antennas[band];

	il4965_toggle_tx_ant(il, &il->scan_tx_ant[band], scan_tx_antennas);
	rate_flags |= BIT(il->scan_tx_ant[band]) << RATE_MCS_ANT_POS;
	scan->tx_cmd.rate_n_flags = cpu_to_le32(rate | rate_flags);

	/* In power save mode use one chain, otherwise use all chains */
	if (test_bit(S_POWER_PMI, &il->status)) {
		/* rx_ant has been set to all valid chains previously */
		active_chains =
		    rx_ant & ((u8) (il->chain_noise_data.active_chains));
		if (!active_chains)
			active_chains = rx_ant;

		D_SCAN("chain_noise_data.active_chains: %u\n",
		       il->chain_noise_data.active_chains);

		rx_ant = il4965_first_antenna(active_chains);
	}

	/* MIMO is not used here, but value is required */
	rx_chain |= il->hw_params.valid_rx_ant << RXON_RX_CHAIN_VALID_POS;
	rx_chain |= rx_ant << RXON_RX_CHAIN_FORCE_MIMO_SEL_POS;
	rx_chain |= rx_ant << RXON_RX_CHAIN_FORCE_SEL_POS;
	rx_chain |= 0x1 << RXON_RX_CHAIN_DRIVER_FORCE_POS;
	scan->rx_chain = cpu_to_le16(rx_chain);

	cmd_len =
	    il_fill_probe_req(il, (struct ieee80211_mgmt *)scan->data,
			      vif->addr, il->scan_request->ie,
			      il->scan_request->ie_len,
			      IL_MAX_SCAN_SIZE - sizeof(*scan));
	scan->tx_cmd.len = cpu_to_le16(cmd_len);

	scan->filter_flags |=
	    (RXON_FILTER_ACCEPT_GRP_MSK | RXON_FILTER_BCON_AWARE_MSK);

	scan->channel_count =
	    il4965_get_channels_for_scan(il, vif, band, is_active, n_probes,
					 (void *)&scan->data[cmd_len]);
	if (scan->channel_count == 0) {
		D_SCAN("channel count %d\n", scan->channel_count);
		return -EIO;
	}

	cmd.len +=
	    le16_to_cpu(scan->tx_cmd.len) +
	    scan->channel_count * sizeof(struct il_scan_channel);
	cmd.data = scan;
	scan->len = cpu_to_le16(cmd.len);

	set_bit(S_SCAN_HW, &il->status);

	ret = il_send_cmd_sync(il, &cmd);
	if (ret)
		clear_bit(S_SCAN_HW, &il->status);

	return ret;
}

int
il4965_manage_ibss_station(struct il_priv *il, struct ieee80211_vif *vif,
			   bool add)
{
	struct il_vif_priv *vif_priv = (void *)vif->drv_priv;

	if (add)
		return il4965_add_bssid_station(il, vif->bss_conf.bssid,
						&vif_priv->ibss_bssid_sta_id);
	return il_remove_station(il, vif_priv->ibss_bssid_sta_id,
				 vif->bss_conf.bssid);
}

void
il4965_free_tfds_in_queue(struct il_priv *il, int sta_id, int tid, int freed)
{
	lockdep_assert_held(&il->sta_lock);

	if (il->stations[sta_id].tid[tid].tfds_in_queue >= freed)
		il->stations[sta_id].tid[tid].tfds_in_queue -= freed;
	else {
		D_TX("free more than tfds_in_queue (%u:%d)\n",
		     il->stations[sta_id].tid[tid].tfds_in_queue, freed);
		il->stations[sta_id].tid[tid].tfds_in_queue = 0;
	}
}

#define IL_TX_QUEUE_MSK	0xfffff

static bool
il4965_is_single_rx_stream(struct il_priv *il)
{
	return il->current_ht_config.smps == IEEE80211_SMPS_STATIC ||
	    il->current_ht_config.single_chain_sufficient;
}

#define IL_NUM_RX_CHAINS_MULTIPLE	3
#define IL_NUM_RX_CHAINS_SINGLE	2
#define IL_NUM_IDLE_CHAINS_DUAL	2
#define IL_NUM_IDLE_CHAINS_SINGLE	1

/*
 * Determine how many receiver/antenna chains to use.
 *
 * More provides better reception via diversity.  Fewer saves power
 * at the expense of throughput, but only when not in powersave to
 * start with.
 *
 * MIMO (dual stream) requires at least 2, but works better with 3.
 * This does not determine *which* chains to use, just how many.
 */
static int
il4965_get_active_rx_chain_count(struct il_priv *il)
{
	/* # of Rx chains to use when expecting MIMO. */
	if (il4965_is_single_rx_stream(il))
		return IL_NUM_RX_CHAINS_SINGLE;
	else
		return IL_NUM_RX_CHAINS_MULTIPLE;
}

/*
 * When we are in power saving mode, unless device support spatial
 * multiplexing power save, use the active count for rx chain count.
 */
static int
il4965_get_idle_rx_chain_count(struct il_priv *il, int active_cnt)
{
	/* # Rx chains when idling, depending on SMPS mode */
	switch (il->current_ht_config.smps) {
	case IEEE80211_SMPS_STATIC:
	case IEEE80211_SMPS_DYNAMIC:
		return IL_NUM_IDLE_CHAINS_SINGLE;
	case IEEE80211_SMPS_OFF:
		return active_cnt;
	default:
		WARN(1, "invalid SMPS mode %d", il->current_ht_config.smps);
		return active_cnt;
	}
}

/* up to 4 chains */
static u8
il4965_count_chain_bitmap(u32 chain_bitmap)
{
	u8 res;
	res = (chain_bitmap & BIT(0)) >> 0;
	res += (chain_bitmap & BIT(1)) >> 1;
	res += (chain_bitmap & BIT(2)) >> 2;
	res += (chain_bitmap & BIT(3)) >> 3;
	return res;
}

/*
 * il4965_set_rxon_chain - Set up Rx chain usage in "staging" RXON image
 *
 * Selects how many and which Rx receivers/antennas/chains to use.
 * This should not be used for scan command ... it puts data in wrong place.
 */
void
il4965_set_rxon_chain(struct il_priv *il)
{
	bool is_single = il4965_is_single_rx_stream(il);
	bool is_cam = !test_bit(S_POWER_PMI, &il->status);
	u8 idle_rx_cnt, active_rx_cnt, valid_rx_cnt;
	u32 active_chains;
	u16 rx_chain;

	/* Tell uCode which antennas are actually connected.
	 * Before first association, we assume all antennas are connected.
	 * Just after first association, il4965_chain_noise_calibration()
	 *    checks which antennas actually *are* connected. */
	if (il->chain_noise_data.active_chains)
		active_chains = il->chain_noise_data.active_chains;
	else
		active_chains = il->hw_params.valid_rx_ant;

	rx_chain = active_chains << RXON_RX_CHAIN_VALID_POS;

	/* How many receivers should we use? */
	active_rx_cnt = il4965_get_active_rx_chain_count(il);
	idle_rx_cnt = il4965_get_idle_rx_chain_count(il, active_rx_cnt);

	/* correct rx chain count according hw settings
	 * and chain noise calibration
	 */
	valid_rx_cnt = il4965_count_chain_bitmap(active_chains);
	if (valid_rx_cnt < active_rx_cnt)
		active_rx_cnt = valid_rx_cnt;

	if (valid_rx_cnt < idle_rx_cnt)
		idle_rx_cnt = valid_rx_cnt;

	rx_chain |= active_rx_cnt << RXON_RX_CHAIN_MIMO_CNT_POS;
	rx_chain |= idle_rx_cnt << RXON_RX_CHAIN_CNT_POS;

	il->staging.rx_chain = cpu_to_le16(rx_chain);

	if (!is_single && active_rx_cnt >= IL_NUM_RX_CHAINS_SINGLE && is_cam)
		il->staging.rx_chain |= RXON_RX_CHAIN_MIMO_FORCE_MSK;
	else
		il->staging.rx_chain &= ~RXON_RX_CHAIN_MIMO_FORCE_MSK;

	D_ASSOC("rx_chain=0x%X active=%d idle=%d\n", il->staging.rx_chain,
		active_rx_cnt, idle_rx_cnt);

	WARN_ON(active_rx_cnt == 0 || idle_rx_cnt == 0 ||
		active_rx_cnt < idle_rx_cnt);
}

static const char *
il4965_get_fh_string(int cmd)
{
	switch (cmd) {
		IL_CMD(FH49_RSCSR_CHNL0_STTS_WPTR_REG);
		IL_CMD(FH49_RSCSR_CHNL0_RBDCB_BASE_REG);
		IL_CMD(FH49_RSCSR_CHNL0_WPTR);
		IL_CMD(FH49_MEM_RCSR_CHNL0_CONFIG_REG);
		IL_CMD(FH49_MEM_RSSR_SHARED_CTRL_REG);
		IL_CMD(FH49_MEM_RSSR_RX_STATUS_REG);
		IL_CMD(FH49_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV);
		IL_CMD(FH49_TSSR_TX_STATUS_REG);
		IL_CMD(FH49_TSSR_TX_ERROR_REG);
	default:
		return "UNKNOWN";
	}
}

int
il4965_dump_fh(struct il_priv *il, char **buf, bool display)
{
	int i;
#ifdef CONFIG_IWLEGACY_DEBUG
	int pos = 0;
	size_t bufsz = 0;
#endif
	static const u32 fh_tbl[] = {
		FH49_RSCSR_CHNL0_STTS_WPTR_REG,
		FH49_RSCSR_CHNL0_RBDCB_BASE_REG,
		FH49_RSCSR_CHNL0_WPTR,
		FH49_MEM_RCSR_CHNL0_CONFIG_REG,
		FH49_MEM_RSSR_SHARED_CTRL_REG,
		FH49_MEM_RSSR_RX_STATUS_REG,
		FH49_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV,
		FH49_TSSR_TX_STATUS_REG,
		FH49_TSSR_TX_ERROR_REG
	};
#ifdef CONFIG_IWLEGACY_DEBUG
	if (display) {
		bufsz = ARRAY_SIZE(fh_tbl) * 48 + 40;
		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;
		pos +=
		    scnprintf(*buf + pos, bufsz - pos, "FH register values:\n");
		for (i = 0; i < ARRAY_SIZE(fh_tbl); i++) {
			pos +=
			    scnprintf(*buf + pos, bufsz - pos,
				      "  %34s: 0X%08x\n",
				      il4965_get_fh_string(fh_tbl[i]),
				      il_rd(il, fh_tbl[i]));
		}
		return pos;
	}
#endif
	IL_ERR("FH register values:\n");
	for (i = 0; i < ARRAY_SIZE(fh_tbl); i++) {
		IL_ERR("  %34s: 0X%08x\n", il4965_get_fh_string(fh_tbl[i]),
		       il_rd(il, fh_tbl[i]));
	}
	return 0;
}

static void
il4965_hdl_missed_beacon(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_missed_beacon_notif *missed_beacon;

	missed_beacon = &pkt->u.missed_beacon;
	if (le32_to_cpu(missed_beacon->consecutive_missed_beacons) >
	    il->missed_beacon_threshold) {
		D_CALIB("missed bcn cnsq %d totl %d rcd %d expctd %d\n",
			le32_to_cpu(missed_beacon->consecutive_missed_beacons),
			le32_to_cpu(missed_beacon->total_missed_becons),
			le32_to_cpu(missed_beacon->num_recvd_beacons),
			le32_to_cpu(missed_beacon->num_expected_beacons));
		if (!test_bit(S_SCANNING, &il->status))
			il4965_init_sensitivity(il);
	}
}

/* Calculate noise level, based on measurements during network silence just
 *   before arriving beacon.  This measurement can be done only if we know
 *   exactly when to expect beacons, therefore only when we're associated. */
static void
il4965_rx_calc_noise(struct il_priv *il)
{
	struct stats_rx_non_phy *rx_info;
	int num_active_rx = 0;
	int total_silence = 0;
	int bcn_silence_a, bcn_silence_b, bcn_silence_c;
	int last_rx_noise;

	rx_info = &(il->_4965.stats.rx.general);
	bcn_silence_a =
	    le32_to_cpu(rx_info->beacon_silence_rssi_a) & IN_BAND_FILTER;
	bcn_silence_b =
	    le32_to_cpu(rx_info->beacon_silence_rssi_b) & IN_BAND_FILTER;
	bcn_silence_c =
	    le32_to_cpu(rx_info->beacon_silence_rssi_c) & IN_BAND_FILTER;

	if (bcn_silence_a) {
		total_silence += bcn_silence_a;
		num_active_rx++;
	}
	if (bcn_silence_b) {
		total_silence += bcn_silence_b;
		num_active_rx++;
	}
	if (bcn_silence_c) {
		total_silence += bcn_silence_c;
		num_active_rx++;
	}

	/* Average among active antennas */
	if (num_active_rx)
		last_rx_noise = (total_silence / num_active_rx) - 107;
	else
		last_rx_noise = IL_NOISE_MEAS_NOT_AVAILABLE;

	D_CALIB("inband silence a %u, b %u, c %u, dBm %d\n", bcn_silence_a,
		bcn_silence_b, bcn_silence_c, last_rx_noise);
}

#ifdef CONFIG_IWLEGACY_DEBUGFS
/*
 *  based on the assumption of all stats counter are in DWORD
 *  FIXME: This function is for debugging, do not deal with
 *  the case of counters roll-over.
 */
static void
il4965_accumulative_stats(struct il_priv *il, __le32 * stats)
{
	int i, size;
	__le32 *prev_stats;
	u32 *accum_stats;
	u32 *delta, *max_delta;
	struct stats_general_common *general, *accum_general;

	prev_stats = (__le32 *) &il->_4965.stats;
	accum_stats = (u32 *) &il->_4965.accum_stats;
	size = sizeof(struct il_notif_stats);
	general = &il->_4965.stats.general.common;
	accum_general = &il->_4965.accum_stats.general.common;
	delta = (u32 *) &il->_4965.delta_stats;
	max_delta = (u32 *) &il->_4965.max_delta;

	for (i = sizeof(__le32); i < size;
	     i +=
	     sizeof(__le32), stats++, prev_stats++, delta++, max_delta++,
	     accum_stats++) {
		if (le32_to_cpu(*stats) > le32_to_cpu(*prev_stats)) {
			*delta =
			    (le32_to_cpu(*stats) - le32_to_cpu(*prev_stats));
			*accum_stats += *delta;
			if (*delta > *max_delta)
				*max_delta = *delta;
		}
	}

	/* reset accumulative stats for "no-counter" type stats */
	accum_general->temperature = general->temperature;
	accum_general->ttl_timestamp = general->ttl_timestamp;
}
#endif

static void
il4965_hdl_stats(struct il_priv *il, struct il_rx_buf *rxb)
{
	const int recalib_seconds = 60;
	bool change;
	struct il_rx_pkt *pkt = rxb_addr(rxb);

	D_RX("Statistics notification received (%d vs %d).\n",
	     (int)sizeof(struct il_notif_stats),
	     le32_to_cpu(pkt->len_n_flags) & IL_RX_FRAME_SIZE_MSK);

	change =
	    ((il->_4965.stats.general.common.temperature !=
	      pkt->u.stats.general.common.temperature) ||
	     ((il->_4965.stats.flag & STATS_REPLY_FLG_HT40_MODE_MSK) !=
	      (pkt->u.stats.flag & STATS_REPLY_FLG_HT40_MODE_MSK)));
#ifdef CONFIG_IWLEGACY_DEBUGFS
	il4965_accumulative_stats(il, (__le32 *) &pkt->u.stats);
#endif

	/* TODO: reading some of stats is unneeded */
	memcpy(&il->_4965.stats, &pkt->u.stats, sizeof(il->_4965.stats));

	set_bit(S_STATS, &il->status);

	/*
	 * Reschedule the stats timer to occur in recalib_seconds to ensure
	 * we get a thermal update even if the uCode doesn't give us one
	 */
	mod_timer(&il->stats_periodic,
		  jiffies + msecs_to_jiffies(recalib_seconds * 1000));

	if (unlikely(!test_bit(S_SCANNING, &il->status)) &&
	    (pkt->hdr.cmd == N_STATS)) {
		il4965_rx_calc_noise(il);
		queue_work(il->workqueue, &il->run_time_calib_work);
	}

	if (change)
		il4965_temperature_calib(il);
}

static void
il4965_hdl_c_stats(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);

	if (le32_to_cpu(pkt->u.stats.flag) & UCODE_STATS_CLEAR_MSK) {
#ifdef CONFIG_IWLEGACY_DEBUGFS
		memset(&il->_4965.accum_stats, 0,
		       sizeof(struct il_notif_stats));
		memset(&il->_4965.delta_stats, 0,
		       sizeof(struct il_notif_stats));
		memset(&il->_4965.max_delta, 0, sizeof(struct il_notif_stats));
#endif
		D_RX("Statistics have been cleared\n");
	}
	il4965_hdl_stats(il, rxb);
}


/*
 * mac80211 queues, ACs, hardware queues, FIFOs.
 *
 * Cf. https://wireless.wiki.kernel.org/en/developers/Documentation/mac80211/queues
 *
 * Mac80211 uses the following numbers, which we get as from it
 * by way of skb_get_queue_mapping(skb):
 *
 *     VO      0
 *     VI      1
 *     BE      2
 *     BK      3
 *
 *
 * Regular (not A-MPDU) frames are put into hardware queues corresponding
 * to the FIFOs, see comments in iwl-prph.h. Aggregated frames get their
 * own queue per aggregation session (RA/TID combination), such queues are
 * set up to map into FIFOs too, for which we need an AC->FIFO mapping. In
 * order to map frames to the right queue, we also need an AC->hw queue
 * mapping. This is implemented here.
 *
 * Due to the way hw queues are set up (by the hw specific modules like
 * 4965.c), the AC->hw queue mapping is the identity
 * mapping.
 */

static const u8 tid_to_ac[] = {
	IEEE80211_AC_BE,
	IEEE80211_AC_BK,
	IEEE80211_AC_BK,
	IEEE80211_AC_BE,
	IEEE80211_AC_VI,
	IEEE80211_AC_VI,
	IEEE80211_AC_VO,
	IEEE80211_AC_VO
};

static inline int
il4965_get_ac_from_tid(u16 tid)
{
	if (likely(tid < ARRAY_SIZE(tid_to_ac)))
		return tid_to_ac[tid];

	/* no support for TIDs 8-15 yet */
	return -EINVAL;
}

static inline int
il4965_get_fifo_from_tid(u16 tid)
{
	static const u8 ac_to_fifo[] = {
		IL_TX_FIFO_VO,
		IL_TX_FIFO_VI,
		IL_TX_FIFO_BE,
		IL_TX_FIFO_BK,
	};

	if (likely(tid < ARRAY_SIZE(tid_to_ac)))
		return ac_to_fifo[tid_to_ac[tid]];

	/* no support for TIDs 8-15 yet */
	return -EINVAL;
}

/*
 * handle build C_TX command notification.
 */
static void
il4965_tx_cmd_build_basic(struct il_priv *il, struct sk_buff *skb,
			  struct il_tx_cmd *tx_cmd,
			  struct ieee80211_tx_info *info,
			  struct ieee80211_hdr *hdr, u8 std_id)
{
	__le16 fc = hdr->frame_control;
	__le32 tx_flags = tx_cmd->tx_flags;

	tx_cmd->stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;
	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
		tx_flags |= TX_CMD_FLG_ACK_MSK;
		if (ieee80211_is_mgmt(fc))
			tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
		if (ieee80211_is_probe_resp(fc) &&
		    !(le16_to_cpu(hdr->seq_ctrl) & 0xf))
			tx_flags |= TX_CMD_FLG_TSF_MSK;
	} else {
		tx_flags &= (~TX_CMD_FLG_ACK_MSK);
		tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
	}

	if (ieee80211_is_back_req(fc))
		tx_flags |= TX_CMD_FLG_ACK_MSK | TX_CMD_FLG_IMM_BA_RSP_MASK;

	tx_cmd->sta_id = std_id;
	if (ieee80211_has_morefrags(fc))
		tx_flags |= TX_CMD_FLG_MORE_FRAG_MSK;

	if (ieee80211_is_data_qos(fc)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		tx_cmd->tid_tspec = qc[0] & 0xf;
		tx_flags &= ~TX_CMD_FLG_SEQ_CTL_MSK;
	} else {
		tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
	}

	il_tx_cmd_protection(il, info, fc, &tx_flags);

	tx_flags &= ~(TX_CMD_FLG_ANT_SEL_MSK);
	if (ieee80211_is_mgmt(fc)) {
		if (ieee80211_is_assoc_req(fc) || ieee80211_is_reassoc_req(fc))
			tx_cmd->timeout.pm_frame_timeout = cpu_to_le16(3);
		else
			tx_cmd->timeout.pm_frame_timeout = cpu_to_le16(2);
	} else {
		tx_cmd->timeout.pm_frame_timeout = 0;
	}

	tx_cmd->driver_txop = 0;
	tx_cmd->tx_flags = tx_flags;
	tx_cmd->next_frame_len = 0;
}

static void
il4965_tx_cmd_build_rate(struct il_priv *il,
			 struct il_tx_cmd *tx_cmd,
			 struct ieee80211_tx_info *info,
			 struct ieee80211_sta *sta,
			 __le16 fc)
{
	const u8 rts_retry_limit = 60;
	u32 rate_flags;
	int rate_idx;
	u8 data_retry_limit;
	u8 rate_plcp;

	/* Set retry limit on DATA packets and Probe Responses */
	if (ieee80211_is_probe_resp(fc))
		data_retry_limit = 3;
	else
		data_retry_limit = IL4965_DEFAULT_TX_RETRY;
	tx_cmd->data_retry_limit = data_retry_limit;
	/* Set retry limit on RTS packets */
	tx_cmd->rts_retry_limit = min(data_retry_limit, rts_retry_limit);

	/* DATA packets will use the uCode station table for rate/antenna
	 * selection */
	if (ieee80211_is_data(fc)) {
		tx_cmd->initial_rate_idx = 0;
		tx_cmd->tx_flags |= TX_CMD_FLG_STA_RATE_MSK;
		return;
	}

	/**
	 * If the current TX rate stored in mac80211 has the MCS bit set, it's
	 * not really a TX rate.  Thus, we use the lowest supported rate for
	 * this band.  Also use the lowest supported rate if the stored rate
	 * idx is invalid.
	 */
	rate_idx = info->control.rates[0].idx;
	if ((info->control.rates[0].flags & IEEE80211_TX_RC_MCS) || rate_idx < 0
	    || rate_idx > RATE_COUNT_LEGACY)
		rate_idx = rate_lowest_index(&il->bands[info->band], sta);
	/* For 5 GHZ band, remap mac80211 rate indices into driver indices */
	if (info->band == NL80211_BAND_5GHZ)
		rate_idx += IL_FIRST_OFDM_RATE;
	/* Get PLCP rate for tx_cmd->rate_n_flags */
	rate_plcp = il_rates[rate_idx].plcp;
	/* Zero out flags for this packet */
	rate_flags = 0;

	/* Set CCK flag as needed */
	if (rate_idx >= IL_FIRST_CCK_RATE && rate_idx <= IL_LAST_CCK_RATE)
		rate_flags |= RATE_MCS_CCK_MSK;

	/* Set up antennas */
	il4965_toggle_tx_ant(il, &il->mgmt_tx_ant, il->hw_params.valid_tx_ant);
	rate_flags |= BIT(il->mgmt_tx_ant) << RATE_MCS_ANT_POS;

	/* Set the rate in the TX cmd */
	tx_cmd->rate_n_flags = cpu_to_le32(rate_plcp | rate_flags);
}

static void
il4965_tx_cmd_build_hwcrypto(struct il_priv *il, struct ieee80211_tx_info *info,
			     struct il_tx_cmd *tx_cmd, struct sk_buff *skb_frag,
			     int sta_id)
{
	struct ieee80211_key_conf *keyconf = info->control.hw_key;

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		tx_cmd->sec_ctl = TX_CMD_SEC_CCM;
		memcpy(tx_cmd->key, keyconf->key, keyconf->keylen);
		if (info->flags & IEEE80211_TX_CTL_AMPDU)
			tx_cmd->tx_flags |= TX_CMD_FLG_AGG_CCMP_MSK;
		D_TX("tx_cmd with AES hwcrypto\n");
		break;

	case WLAN_CIPHER_SUITE_TKIP:
		tx_cmd->sec_ctl = TX_CMD_SEC_TKIP;
		ieee80211_get_tkip_p2k(keyconf, skb_frag, tx_cmd->key);
		D_TX("tx_cmd with tkip hwcrypto\n");
		break;

	case WLAN_CIPHER_SUITE_WEP104:
		tx_cmd->sec_ctl |= TX_CMD_SEC_KEY128;
		fallthrough;
	case WLAN_CIPHER_SUITE_WEP40:
		tx_cmd->sec_ctl |=
		    (TX_CMD_SEC_WEP | (keyconf->keyidx & TX_CMD_SEC_MSK) <<
		     TX_CMD_SEC_SHIFT);

		memcpy(&tx_cmd->key[3], keyconf->key, keyconf->keylen);

		D_TX("Configuring packet for WEP encryption " "with key %d\n",
		     keyconf->keyidx);
		break;

	default:
		IL_ERR("Unknown encode cipher %x\n", keyconf->cipher);
		break;
	}
}

/*
 * start C_TX command process
 */
int
il4965_tx_skb(struct il_priv *il,
	      struct ieee80211_sta *sta,
	      struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct il_station_priv *sta_priv = NULL;
	struct il_tx_queue *txq;
	struct il_queue *q;
	struct il_device_cmd *out_cmd;
	struct il_cmd_meta *out_meta;
	struct il_tx_cmd *tx_cmd;
	int txq_id;
	dma_addr_t phys_addr;
	dma_addr_t txcmd_phys;
	dma_addr_t scratch_phys;
	u16 len, firstlen, secondlen;
	u16 seq_number = 0;
	__le16 fc;
	u8 hdr_len;
	u8 sta_id;
	u8 wait_write_ptr = 0;
	u8 tid = 0;
	u8 *qc = NULL;
	unsigned long flags;
	bool is_agg = false;

	spin_lock_irqsave(&il->lock, flags);
	if (il_is_rfkill(il)) {
		D_DROP("Dropping - RF KILL\n");
		goto drop_unlock;
	}

	fc = hdr->frame_control;

#ifdef CONFIG_IWLEGACY_DEBUG
	if (ieee80211_is_auth(fc))
		D_TX("Sending AUTH frame\n");
	else if (ieee80211_is_assoc_req(fc))
		D_TX("Sending ASSOC frame\n");
	else if (ieee80211_is_reassoc_req(fc))
		D_TX("Sending REASSOC frame\n");
#endif

	hdr_len = ieee80211_hdrlen(fc);

	/* For management frames use broadcast id to do not break aggregation */
	if (!ieee80211_is_data(fc))
		sta_id = il->hw_params.bcast_id;
	else {
		/* Find idx into station table for destination station */
		sta_id = il_sta_id_or_broadcast(il, sta);

		if (sta_id == IL_INVALID_STATION) {
			D_DROP("Dropping - INVALID STATION: %pM\n", hdr->addr1);
			goto drop_unlock;
		}
	}

	D_TX("station Id %d\n", sta_id);

	if (sta)
		sta_priv = (void *)sta->drv_priv;

	if (sta_priv && sta_priv->asleep &&
	    (info->flags & IEEE80211_TX_CTL_NO_PS_BUFFER)) {
		/*
		 * This sends an asynchronous command to the device,
		 * but we can rely on it being processed before the
		 * next frame is processed -- and the next frame to
		 * this station is the one that will consume this
		 * counter.
		 * For now set the counter to just 1 since we do not
		 * support uAPSD yet.
		 */
		il4965_sta_modify_sleep_tx_count(il, sta_id, 1);
	}

	/* FIXME: remove me ? */
	WARN_ON_ONCE(info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM);

	/* Access category (AC) is also the queue number */
	txq_id = skb_get_queue_mapping(skb);

	/* irqs already disabled/saved above when locking il->lock */
	spin_lock(&il->sta_lock);

	if (ieee80211_is_data_qos(fc)) {
		qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
		if (WARN_ON_ONCE(tid >= MAX_TID_COUNT)) {
			spin_unlock(&il->sta_lock);
			goto drop_unlock;
		}
		seq_number = il->stations[sta_id].tid[tid].seq_number;
		seq_number &= IEEE80211_SCTL_SEQ;
		hdr->seq_ctrl =
		    hdr->seq_ctrl & cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(seq_number);
		seq_number += 0x10;
		/* aggregation is on for this <sta,tid> */
		if (info->flags & IEEE80211_TX_CTL_AMPDU &&
		    il->stations[sta_id].tid[tid].agg.state == IL_AGG_ON) {
			txq_id = il->stations[sta_id].tid[tid].agg.txq_id;
			is_agg = true;
		}
	}

	txq = &il->txq[txq_id];
	q = &txq->q;

	if (unlikely(il_queue_space(q) < q->high_mark)) {
		spin_unlock(&il->sta_lock);
		goto drop_unlock;
	}

	if (ieee80211_is_data_qos(fc)) {
		il->stations[sta_id].tid[tid].tfds_in_queue++;
		if (!ieee80211_has_morefrags(fc))
			il->stations[sta_id].tid[tid].seq_number = seq_number;
	}

	spin_unlock(&il->sta_lock);

	txq->skbs[q->write_ptr] = skb;

	/* Set up first empty entry in queue's array of Tx/cmd buffers */
	out_cmd = txq->cmd[q->write_ptr];
	out_meta = &txq->meta[q->write_ptr];
	tx_cmd = &out_cmd->cmd.tx;
	memset(&out_cmd->hdr, 0, sizeof(out_cmd->hdr));
	memset(tx_cmd, 0, sizeof(struct il_tx_cmd));

	/*
	 * Set up the Tx-command (not MAC!) header.
	 * Store the chosen Tx queue and TFD idx within the sequence field;
	 * after Tx, uCode's Tx response will return this value so driver can
	 * locate the frame within the tx queue and do post-tx processing.
	 */
	out_cmd->hdr.cmd = C_TX;
	out_cmd->hdr.sequence =
	    cpu_to_le16((u16)
			(QUEUE_TO_SEQ(txq_id) | IDX_TO_SEQ(q->write_ptr)));

	/* Copy MAC header from skb into command buffer */
	memcpy(tx_cmd->hdr, hdr, hdr_len);

	/* Total # bytes to be transmitted */
	tx_cmd->len = cpu_to_le16((u16) skb->len);

	if (info->control.hw_key)
		il4965_tx_cmd_build_hwcrypto(il, info, tx_cmd, skb, sta_id);

	/* TODO need this for burst mode later on */
	il4965_tx_cmd_build_basic(il, skb, tx_cmd, info, hdr, sta_id);

	il4965_tx_cmd_build_rate(il, tx_cmd, info, sta, fc);

	/*
	 * Use the first empty entry in this queue's command buffer array
	 * to contain the Tx command and MAC header concatenated together
	 * (payload data will be in another buffer).
	 * Size of this varies, due to varying MAC header length.
	 * If end is not dword aligned, we'll have 2 extra bytes at the end
	 * of the MAC header (device reads on dword boundaries).
	 * We'll tell device about this padding later.
	 */
	len = sizeof(struct il_tx_cmd) + sizeof(struct il_cmd_header) + hdr_len;
	firstlen = (len + 3) & ~3;

	/* Tell NIC about any 2-byte padding after MAC header */
	if (firstlen != len)
		tx_cmd->tx_flags |= TX_CMD_FLG_MH_PAD_MSK;

	/* Physical address of this Tx command's header (not MAC header!),
	 * within command buffer array. */
	txcmd_phys = dma_map_single(&il->pci_dev->dev, &out_cmd->hdr, firstlen,
				    DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(&il->pci_dev->dev, txcmd_phys)))
		goto drop_unlock;

	/* Set up TFD's 2nd entry to point directly to remainder of skb,
	 * if any (802.11 null frames have no payload). */
	secondlen = skb->len - hdr_len;
	if (secondlen > 0) {
		phys_addr = dma_map_single(&il->pci_dev->dev, skb->data + hdr_len,
					   secondlen, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(&il->pci_dev->dev, phys_addr)))
			goto drop_unlock;
	}

	/* Add buffer containing Tx command and MAC(!) header to TFD's
	 * first entry */
	il->ops->txq_attach_buf_to_tfd(il, txq, txcmd_phys, firstlen, 1, 0);
	dma_unmap_addr_set(out_meta, mapping, txcmd_phys);
	dma_unmap_len_set(out_meta, len, firstlen);
	if (secondlen)
		il->ops->txq_attach_buf_to_tfd(il, txq, phys_addr, secondlen,
					       0, 0);

	if (!ieee80211_has_morefrags(hdr->frame_control)) {
		txq->need_update = 1;
	} else {
		wait_write_ptr = 1;
		txq->need_update = 0;
	}

	scratch_phys =
	    txcmd_phys + sizeof(struct il_cmd_header) +
	    offsetof(struct il_tx_cmd, scratch);

	/* take back ownership of DMA buffer to enable update */
	dma_sync_single_for_cpu(&il->pci_dev->dev, txcmd_phys, firstlen,
				DMA_BIDIRECTIONAL);
	tx_cmd->dram_lsb_ptr = cpu_to_le32(scratch_phys);
	tx_cmd->dram_msb_ptr = il_get_dma_hi_addr(scratch_phys);

	il_update_stats(il, true, fc, skb->len);

	D_TX("sequence nr = 0X%x\n", le16_to_cpu(out_cmd->hdr.sequence));
	D_TX("tx_flags = 0X%x\n", le32_to_cpu(tx_cmd->tx_flags));
	il_print_hex_dump(il, IL_DL_TX, (u8 *) tx_cmd, sizeof(*tx_cmd));
	il_print_hex_dump(il, IL_DL_TX, (u8 *) tx_cmd->hdr, hdr_len);

	/* Set up entry for this TFD in Tx byte-count array */
	if (info->flags & IEEE80211_TX_CTL_AMPDU)
		il->ops->txq_update_byte_cnt_tbl(il, txq, le16_to_cpu(tx_cmd->len));

	dma_sync_single_for_device(&il->pci_dev->dev, txcmd_phys, firstlen,
				   DMA_BIDIRECTIONAL);

	/* Tell device the write idx *just past* this latest filled TFD */
	q->write_ptr = il_queue_inc_wrap(q->write_ptr, q->n_bd);
	il_txq_update_write_ptr(il, txq);
	spin_unlock_irqrestore(&il->lock, flags);

	/*
	 * At this point the frame is "transmitted" successfully
	 * and we will get a TX status notification eventually,
	 * regardless of the value of ret. "ret" only indicates
	 * whether or not we should update the write pointer.
	 */

	/*
	 * Avoid atomic ops if it isn't an associated client.
	 * Also, if this is a packet for aggregation, don't
	 * increase the counter because the ucode will stop
	 * aggregation queues when their respective station
	 * goes to sleep.
	 */
	if (sta_priv && sta_priv->client && !is_agg)
		atomic_inc(&sta_priv->pending_frames);

	if (il_queue_space(q) < q->high_mark && il->mac80211_registered) {
		if (wait_write_ptr) {
			spin_lock_irqsave(&il->lock, flags);
			txq->need_update = 1;
			il_txq_update_write_ptr(il, txq);
			spin_unlock_irqrestore(&il->lock, flags);
		} else {
			il_stop_queue(il, txq);
		}
	}

	return 0;

drop_unlock:
	spin_unlock_irqrestore(&il->lock, flags);
	return -1;
}

static inline int
il4965_alloc_dma_ptr(struct il_priv *il, struct il_dma_ptr *ptr, size_t size)
{
	ptr->addr = dma_alloc_coherent(&il->pci_dev->dev, size, &ptr->dma,
				       GFP_KERNEL);
	if (!ptr->addr)
		return -ENOMEM;
	ptr->size = size;
	return 0;
}

static inline void
il4965_free_dma_ptr(struct il_priv *il, struct il_dma_ptr *ptr)
{
	if (unlikely(!ptr->addr))
		return;

	dma_free_coherent(&il->pci_dev->dev, ptr->size, ptr->addr, ptr->dma);
	memset(ptr, 0, sizeof(*ptr));
}

/*
 * il4965_hw_txq_ctx_free - Free TXQ Context
 *
 * Destroy all TX DMA queues and structures
 */
void
il4965_hw_txq_ctx_free(struct il_priv *il)
{
	int txq_id;

	/* Tx queues */
	if (il->txq) {
		for (txq_id = 0; txq_id < il->hw_params.max_txq_num; txq_id++)
			if (txq_id == il->cmd_queue)
				il_cmd_queue_free(il);
			else
				il_tx_queue_free(il, txq_id);
	}
	il4965_free_dma_ptr(il, &il->kw);

	il4965_free_dma_ptr(il, &il->scd_bc_tbls);

	/* free tx queue structure */
	il_free_txq_mem(il);
}

/*
 * il4965_txq_ctx_alloc - allocate TX queue context
 * Allocate all Tx DMA structures and initialize them
 */
int
il4965_txq_ctx_alloc(struct il_priv *il)
{
	int ret, txq_id;
	unsigned long flags;

	/* Free all tx/cmd queues and keep-warm buffer */
	il4965_hw_txq_ctx_free(il);

	ret =
	    il4965_alloc_dma_ptr(il, &il->scd_bc_tbls,
				 il->hw_params.scd_bc_tbls_size);
	if (ret) {
		IL_ERR("Scheduler BC Table allocation failed\n");
		goto error_bc_tbls;
	}
	/* Alloc keep-warm buffer */
	ret = il4965_alloc_dma_ptr(il, &il->kw, IL_KW_SIZE);
	if (ret) {
		IL_ERR("Keep Warm allocation failed\n");
		goto error_kw;
	}

	/* allocate tx queue structure */
	ret = il_alloc_txq_mem(il);
	if (ret)
		goto error;

	spin_lock_irqsave(&il->lock, flags);

	/* Turn off all Tx DMA fifos */
	il4965_txq_set_sched(il, 0);

	/* Tell NIC where to find the "keep warm" buffer */
	il_wr(il, FH49_KW_MEM_ADDR_REG, il->kw.dma >> 4);

	spin_unlock_irqrestore(&il->lock, flags);

	/* Alloc and init all Tx queues, including the command queue (#4/#9) */
	for (txq_id = 0; txq_id < il->hw_params.max_txq_num; txq_id++) {
		ret = il_tx_queue_init(il, txq_id);
		if (ret) {
			IL_ERR("Tx %d queue init failed\n", txq_id);
			goto error;
		}
	}

	return ret;

error:
	il4965_hw_txq_ctx_free(il);
	il4965_free_dma_ptr(il, &il->kw);
error_kw:
	il4965_free_dma_ptr(il, &il->scd_bc_tbls);
error_bc_tbls:
	return ret;
}

void
il4965_txq_ctx_reset(struct il_priv *il)
{
	int txq_id;
	unsigned long flags;

	spin_lock_irqsave(&il->lock, flags);

	/* Turn off all Tx DMA fifos */
	il4965_txq_set_sched(il, 0);
	/* Tell NIC where to find the "keep warm" buffer */
	il_wr(il, FH49_KW_MEM_ADDR_REG, il->kw.dma >> 4);

	spin_unlock_irqrestore(&il->lock, flags);

	/* Alloc and init all Tx queues, including the command queue (#4) */
	for (txq_id = 0; txq_id < il->hw_params.max_txq_num; txq_id++)
		il_tx_queue_reset(il, txq_id);
}

static void
il4965_txq_ctx_unmap(struct il_priv *il)
{
	int txq_id;

	if (!il->txq)
		return;

	/* Unmap DMA from host system and free skb's */
	for (txq_id = 0; txq_id < il->hw_params.max_txq_num; txq_id++)
		if (txq_id == il->cmd_queue)
			il_cmd_queue_unmap(il);
		else
			il_tx_queue_unmap(il, txq_id);
}

/*
 * il4965_txq_ctx_stop - Stop all Tx DMA channels
 */
void
il4965_txq_ctx_stop(struct il_priv *il)
{
	int ch, ret;

	_il_wr_prph(il, IL49_SCD_TXFACT, 0);

	/* Stop each Tx DMA channel, and wait for it to be idle */
	for (ch = 0; ch < il->hw_params.dma_chnl_num; ch++) {
		_il_wr(il, FH49_TCSR_CHNL_TX_CONFIG_REG(ch), 0x0);
		ret =
		    _il_poll_bit(il, FH49_TSSR_TX_STATUS_REG,
				 FH49_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(ch),
				 FH49_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(ch),
				 1000);
		if (ret < 0)
			IL_ERR("Timeout stopping DMA channel %d [0x%08x]",
			       ch, _il_rd(il, FH49_TSSR_TX_STATUS_REG));
	}
}

/*
 * Find first available (lowest unused) Tx Queue, mark it "active".
 * Called only when finding queue for aggregation.
 * Should never return anything < 7, because they should already
 * be in use as EDCA AC (0-3), Command (4), reserved (5, 6)
 */
static int
il4965_txq_ctx_activate_free(struct il_priv *il)
{
	int txq_id;

	for (txq_id = 0; txq_id < il->hw_params.max_txq_num; txq_id++)
		if (!test_and_set_bit(txq_id, &il->txq_ctx_active_msk))
			return txq_id;
	return -1;
}

/*
 * il4965_tx_queue_stop_scheduler - Stop queue, but keep configuration
 */
static void
il4965_tx_queue_stop_scheduler(struct il_priv *il, u16 txq_id)
{
	/* Simply stop the queue, but don't change any configuration;
	 * the SCD_ACT_EN bit is the write-enable mask for the ACTIVE bit. */
	il_wr_prph(il, IL49_SCD_QUEUE_STATUS_BITS(txq_id),
		   (0 << IL49_SCD_QUEUE_STTS_REG_POS_ACTIVE) |
		   (1 << IL49_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN));
}

/*
 * il4965_tx_queue_set_q2ratid - Map unique receiver/tid combination to a queue
 */
static int
il4965_tx_queue_set_q2ratid(struct il_priv *il, u16 ra_tid, u16 txq_id)
{
	u32 tbl_dw_addr;
	u32 tbl_dw;
	u16 scd_q2ratid;

	scd_q2ratid = ra_tid & IL_SCD_QUEUE_RA_TID_MAP_RATID_MSK;

	tbl_dw_addr =
	    il->scd_base_addr + IL49_SCD_TRANSLATE_TBL_OFFSET_QUEUE(txq_id);

	tbl_dw = il_read_targ_mem(il, tbl_dw_addr);

	if (txq_id & 0x1)
		tbl_dw = (scd_q2ratid << 16) | (tbl_dw & 0x0000FFFF);
	else
		tbl_dw = scd_q2ratid | (tbl_dw & 0xFFFF0000);

	il_write_targ_mem(il, tbl_dw_addr, tbl_dw);

	return 0;
}

/*
 * il4965_tx_queue_agg_enable - Set up & enable aggregation for selected queue
 *
 * NOTE:  txq_id must be greater than IL49_FIRST_AMPDU_QUEUE,
 *        i.e. it must be one of the higher queues used for aggregation
 */
static int
il4965_txq_agg_enable(struct il_priv *il, int txq_id, int tx_fifo, int sta_id,
		      int tid, u16 ssn_idx)
{
	unsigned long flags;
	u16 ra_tid;
	int ret;

	if ((IL49_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IL49_FIRST_AMPDU_QUEUE +
	     il->cfg->num_of_ampdu_queues <= txq_id)) {
		IL_WARN("queue number out of range: %d, must be %d to %d\n",
			txq_id, IL49_FIRST_AMPDU_QUEUE,
			IL49_FIRST_AMPDU_QUEUE +
			il->cfg->num_of_ampdu_queues - 1);
		return -EINVAL;
	}

	ra_tid = BUILD_RAxTID(sta_id, tid);

	/* Modify device's station table to Tx this TID */
	ret = il4965_sta_tx_modify_enable_tid(il, sta_id, tid);
	if (ret)
		return ret;

	spin_lock_irqsave(&il->lock, flags);

	/* Stop this Tx queue before configuring it */
	il4965_tx_queue_stop_scheduler(il, txq_id);

	/* Map receiver-address / traffic-ID to this queue */
	il4965_tx_queue_set_q2ratid(il, ra_tid, txq_id);

	/* Set this queue as a chain-building queue */
	il_set_bits_prph(il, IL49_SCD_QUEUECHAIN_SEL, (1 << txq_id));

	/* Place first TFD at idx corresponding to start sequence number.
	 * Assumes that ssn_idx is valid (!= 0xFFF) */
	il->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	il->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	il4965_set_wr_ptrs(il, txq_id, ssn_idx);

	/* Set up Tx win size and frame limit for this queue */
	il_write_targ_mem(il,
			  il->scd_base_addr +
			  IL49_SCD_CONTEXT_QUEUE_OFFSET(txq_id),
			  (SCD_WIN_SIZE << IL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_POS)
			  & IL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_MSK);

	il_write_targ_mem(il,
			  il->scd_base_addr +
			  IL49_SCD_CONTEXT_QUEUE_OFFSET(txq_id) + sizeof(u32),
			  (SCD_FRAME_LIMIT <<
			   IL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
			  IL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK);

	il_set_bits_prph(il, IL49_SCD_INTERRUPT_MASK, (1 << txq_id));

	/* Set up Status area in SRAM, map to Tx DMA/FIFO, activate the queue */
	il4965_tx_queue_set_status(il, &il->txq[txq_id], tx_fifo, 1);

	spin_unlock_irqrestore(&il->lock, flags);

	return 0;
}

int
il4965_tx_agg_start(struct il_priv *il, struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta, u16 tid, u16 * ssn)
{
	int sta_id;
	int tx_fifo;
	int txq_id;
	int ret;
	unsigned long flags;
	struct il_tid_data *tid_data;

	/* FIXME: warning if tx fifo not found ? */
	tx_fifo = il4965_get_fifo_from_tid(tid);
	if (unlikely(tx_fifo < 0))
		return tx_fifo;

	D_HT("%s on ra = %pM tid = %d\n", __func__, sta->addr, tid);

	sta_id = il_sta_id(sta);
	if (sta_id == IL_INVALID_STATION) {
		IL_ERR("Start AGG on invalid station\n");
		return -ENXIO;
	}
	if (unlikely(tid >= MAX_TID_COUNT))
		return -EINVAL;

	if (il->stations[sta_id].tid[tid].agg.state != IL_AGG_OFF) {
		IL_ERR("Start AGG when state is not IL_AGG_OFF !\n");
		return -ENXIO;
	}

	txq_id = il4965_txq_ctx_activate_free(il);
	if (txq_id == -1) {
		IL_ERR("No free aggregation queue available\n");
		return -ENXIO;
	}

	spin_lock_irqsave(&il->sta_lock, flags);
	tid_data = &il->stations[sta_id].tid[tid];
	*ssn = IEEE80211_SEQ_TO_SN(tid_data->seq_number);
	tid_data->agg.txq_id = txq_id;
	il_set_swq_id(&il->txq[txq_id], il4965_get_ac_from_tid(tid), txq_id);
	spin_unlock_irqrestore(&il->sta_lock, flags);

	ret = il4965_txq_agg_enable(il, txq_id, tx_fifo, sta_id, tid, *ssn);
	if (ret)
		return ret;

	spin_lock_irqsave(&il->sta_lock, flags);
	tid_data = &il->stations[sta_id].tid[tid];
	if (tid_data->tfds_in_queue == 0) {
		D_HT("HW queue is empty\n");
		tid_data->agg.state = IL_AGG_ON;
		ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
	} else {
		D_HT("HW queue is NOT empty: %d packets in HW queue\n",
		     tid_data->tfds_in_queue);
		tid_data->agg.state = IL_EMPTYING_HW_QUEUE_ADDBA;
	}
	spin_unlock_irqrestore(&il->sta_lock, flags);
	return ret;
}

/*
 * txq_id must be greater than IL49_FIRST_AMPDU_QUEUE
 * il->lock must be held by the caller
 */
static int
il4965_txq_agg_disable(struct il_priv *il, u16 txq_id, u16 ssn_idx, u8 tx_fifo)
{
	if ((IL49_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IL49_FIRST_AMPDU_QUEUE +
	     il->cfg->num_of_ampdu_queues <= txq_id)) {
		IL_WARN("queue number out of range: %d, must be %d to %d\n",
			txq_id, IL49_FIRST_AMPDU_QUEUE,
			IL49_FIRST_AMPDU_QUEUE +
			il->cfg->num_of_ampdu_queues - 1);
		return -EINVAL;
	}

	il4965_tx_queue_stop_scheduler(il, txq_id);

	il_clear_bits_prph(il, IL49_SCD_QUEUECHAIN_SEL, (1 << txq_id));

	il->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	il->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	/* supposes that ssn_idx is valid (!= 0xFFF) */
	il4965_set_wr_ptrs(il, txq_id, ssn_idx);

	il_clear_bits_prph(il, IL49_SCD_INTERRUPT_MASK, (1 << txq_id));
	il_txq_ctx_deactivate(il, txq_id);
	il4965_tx_queue_set_status(il, &il->txq[txq_id], tx_fifo, 0);

	return 0;
}

int
il4965_tx_agg_stop(struct il_priv *il, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta, u16 tid)
{
	int tx_fifo_id, txq_id, sta_id, ssn;
	struct il_tid_data *tid_data;
	int write_ptr, read_ptr;
	unsigned long flags;

	/* FIXME: warning if tx_fifo_id not found ? */
	tx_fifo_id = il4965_get_fifo_from_tid(tid);
	if (unlikely(tx_fifo_id < 0))
		return tx_fifo_id;

	sta_id = il_sta_id(sta);

	if (sta_id == IL_INVALID_STATION) {
		IL_ERR("Invalid station for AGG tid %d\n", tid);
		return -ENXIO;
	}

	spin_lock_irqsave(&il->sta_lock, flags);

	tid_data = &il->stations[sta_id].tid[tid];
	ssn = (tid_data->seq_number & IEEE80211_SCTL_SEQ) >> 4;
	txq_id = tid_data->agg.txq_id;

	switch (il->stations[sta_id].tid[tid].agg.state) {
	case IL_EMPTYING_HW_QUEUE_ADDBA:
		/*
		 * This can happen if the peer stops aggregation
		 * again before we've had a chance to drain the
		 * queue we selected previously, i.e. before the
		 * session was really started completely.
		 */
		D_HT("AGG stop before setup done\n");
		goto turn_off;
	case IL_AGG_ON:
		break;
	default:
		IL_WARN("Stopping AGG while state not ON or starting\n");
	}

	write_ptr = il->txq[txq_id].q.write_ptr;
	read_ptr = il->txq[txq_id].q.read_ptr;

	/* The queue is not empty */
	if (write_ptr != read_ptr) {
		D_HT("Stopping a non empty AGG HW QUEUE\n");
		il->stations[sta_id].tid[tid].agg.state =
		    IL_EMPTYING_HW_QUEUE_DELBA;
		spin_unlock_irqrestore(&il->sta_lock, flags);
		return 0;
	}

	D_HT("HW queue is empty\n");
turn_off:
	il->stations[sta_id].tid[tid].agg.state = IL_AGG_OFF;

	/* do not restore/save irqs */
	spin_unlock(&il->sta_lock);
	spin_lock(&il->lock);

	/*
	 * the only reason this call can fail is queue number out of range,
	 * which can happen if uCode is reloaded and all the station
	 * information are lost. if it is outside the range, there is no need
	 * to deactivate the uCode queue, just return "success" to allow
	 *  mac80211 to clean up it own data.
	 */
	il4965_txq_agg_disable(il, txq_id, ssn, tx_fifo_id);
	spin_unlock_irqrestore(&il->lock, flags);

	ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);

	return 0;
}

int
il4965_txq_check_empty(struct il_priv *il, int sta_id, u8 tid, int txq_id)
{
	struct il_queue *q = &il->txq[txq_id].q;
	u8 *addr = il->stations[sta_id].sta.sta.addr;
	struct il_tid_data *tid_data = &il->stations[sta_id].tid[tid];

	lockdep_assert_held(&il->sta_lock);

	switch (il->stations[sta_id].tid[tid].agg.state) {
	case IL_EMPTYING_HW_QUEUE_DELBA:
		/* We are reclaiming the last packet of the */
		/* aggregated HW queue */
		if (txq_id == tid_data->agg.txq_id &&
		    q->read_ptr == q->write_ptr) {
			u16 ssn = IEEE80211_SEQ_TO_SN(tid_data->seq_number);
			int tx_fifo = il4965_get_fifo_from_tid(tid);
			D_HT("HW queue empty: continue DELBA flow\n");
			il4965_txq_agg_disable(il, txq_id, ssn, tx_fifo);
			tid_data->agg.state = IL_AGG_OFF;
			ieee80211_stop_tx_ba_cb_irqsafe(il->vif, addr, tid);
		}
		break;
	case IL_EMPTYING_HW_QUEUE_ADDBA:
		/* We are reclaiming the last packet of the queue */
		if (tid_data->tfds_in_queue == 0) {
			D_HT("HW queue empty: continue ADDBA flow\n");
			tid_data->agg.state = IL_AGG_ON;
			ieee80211_start_tx_ba_cb_irqsafe(il->vif, addr, tid);
		}
		break;
	}

	return 0;
}

static void
il4965_non_agg_tx_status(struct il_priv *il, const u8 *addr1)
{
	struct ieee80211_sta *sta;
	struct il_station_priv *sta_priv;

	rcu_read_lock();
	sta = ieee80211_find_sta(il->vif, addr1);
	if (sta) {
		sta_priv = (void *)sta->drv_priv;
		/* avoid atomic ops if this isn't a client */
		if (sta_priv->client &&
		    atomic_dec_return(&sta_priv->pending_frames) == 0)
			ieee80211_sta_block_awake(il->hw, sta, false);
	}
	rcu_read_unlock();
}

static void
il4965_tx_status(struct il_priv *il, struct sk_buff *skb, bool is_agg)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!is_agg)
		il4965_non_agg_tx_status(il, hdr->addr1);

	ieee80211_tx_status_irqsafe(il->hw, skb);
}

int
il4965_tx_queue_reclaim(struct il_priv *il, int txq_id, int idx)
{
	struct il_tx_queue *txq = &il->txq[txq_id];
	struct il_queue *q = &txq->q;
	int nfreed = 0;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb;

	if (idx >= q->n_bd || il_queue_used(q, idx) == 0) {
		IL_ERR("Read idx for DMA queue txq id (%d), idx %d, "
		       "is out of range [0-%d] %d %d.\n", txq_id, idx, q->n_bd,
		       q->write_ptr, q->read_ptr);
		return 0;
	}

	for (idx = il_queue_inc_wrap(idx, q->n_bd); q->read_ptr != idx;
	     q->read_ptr = il_queue_inc_wrap(q->read_ptr, q->n_bd)) {

		skb = txq->skbs[txq->q.read_ptr];

		if (WARN_ON_ONCE(skb == NULL))
			continue;

		hdr = (struct ieee80211_hdr *) skb->data;
		if (ieee80211_is_data_qos(hdr->frame_control))
			nfreed++;

		il4965_tx_status(il, skb, txq_id >= IL4965_FIRST_AMPDU_QUEUE);

		txq->skbs[txq->q.read_ptr] = NULL;
		il->ops->txq_free_tfd(il, txq);
	}
	return nfreed;
}

/*
 * il4965_tx_status_reply_compressed_ba - Update tx status from block-ack
 *
 * Go through block-ack's bitmap of ACK'd frames, update driver's record of
 * ACK vs. not.  This gets sent to mac80211, then to rate scaling algo.
 */
static int
il4965_tx_status_reply_compressed_ba(struct il_priv *il, struct il_ht_agg *agg,
				     struct il_compressed_ba_resp *ba_resp)
{
	int i, sh, ack;
	u16 seq_ctl = le16_to_cpu(ba_resp->seq_ctl);
	u16 scd_flow = le16_to_cpu(ba_resp->scd_flow);
	int successes = 0;
	struct ieee80211_tx_info *info;
	u64 bitmap, sent_bitmap;

	if (unlikely(!agg->wait_for_ba)) {
		if (unlikely(ba_resp->bitmap))
			IL_ERR("Received BA when not expected\n");
		return -EINVAL;
	}

	/* Mark that the expected block-ack response arrived */
	agg->wait_for_ba = 0;
	D_TX_REPLY("BA %d %d\n", agg->start_idx, ba_resp->seq_ctl);

	/* Calculate shift to align block-ack bits with our Tx win bits */
	sh = agg->start_idx - SEQ_TO_IDX(seq_ctl >> 4);
	if (sh < 0)		/* tbw something is wrong with indices */
		sh += 0x100;

	if (agg->frame_count > (64 - sh)) {
		D_TX_REPLY("more frames than bitmap size");
		return -1;
	}

	/* don't use 64-bit values for now */
	bitmap = le64_to_cpu(ba_resp->bitmap) >> sh;

	/* check for success or failure according to the
	 * transmitted bitmap and block-ack bitmap */
	sent_bitmap = bitmap & agg->bitmap;

	/* For each frame attempted in aggregation,
	 * update driver's record of tx frame's status. */
	i = 0;
	while (sent_bitmap) {
		ack = sent_bitmap & 1ULL;
		successes += ack;
		D_TX_REPLY("%s ON i=%d idx=%d raw=%d\n", ack ? "ACK" : "NACK",
			   i, (agg->start_idx + i) & 0xff, agg->start_idx + i);
		sent_bitmap >>= 1;
		++i;
	}

	D_TX_REPLY("Bitmap %llx\n", (unsigned long long)bitmap);

	info = IEEE80211_SKB_CB(il->txq[scd_flow].skbs[agg->start_idx]);
	memset(&info->status, 0, sizeof(info->status));
	info->flags |= IEEE80211_TX_STAT_ACK;
	info->flags |= IEEE80211_TX_STAT_AMPDU;
	info->status.ampdu_ack_len = successes;
	info->status.ampdu_len = agg->frame_count;
	il4965_hwrate_to_tx_control(il, agg->rate_n_flags, info);

	return 0;
}

static inline bool
il4965_is_tx_success(u32 status)
{
	status &= TX_STATUS_MSK;
	return (status == TX_STATUS_SUCCESS || status == TX_STATUS_DIRECT_DONE);
}

static u8
il4965_find_station(struct il_priv *il, const u8 *addr)
{
	int i;
	int start = 0;
	int ret = IL_INVALID_STATION;
	unsigned long flags;

	if (il->iw_mode == NL80211_IFTYPE_ADHOC)
		start = IL_STA_ID;

	if (is_broadcast_ether_addr(addr))
		return il->hw_params.bcast_id;

	spin_lock_irqsave(&il->sta_lock, flags);
	for (i = start; i < il->hw_params.max_stations; i++)
		if (il->stations[i].used &&
		    ether_addr_equal(il->stations[i].sta.sta.addr, addr)) {
			ret = i;
			goto out;
		}

	D_ASSOC("can not find STA %pM total %d\n", addr, il->num_stations);

out:
	/*
	 * It may be possible that more commands interacting with stations
	 * arrive before we completed processing the adding of
	 * station
	 */
	if (ret != IL_INVALID_STATION &&
	    (!(il->stations[ret].used & IL_STA_UCODE_ACTIVE) ||
	      (il->stations[ret].used & IL_STA_UCODE_INPROGRESS))) {
		IL_ERR("Requested station info for sta %d before ready.\n",
		       ret);
		ret = IL_INVALID_STATION;
	}
	spin_unlock_irqrestore(&il->sta_lock, flags);
	return ret;
}

static int
il4965_get_ra_sta_id(struct il_priv *il, struct ieee80211_hdr *hdr)
{
	if (il->iw_mode == NL80211_IFTYPE_STATION)
		return IL_AP_ID;
	else {
		u8 *da = ieee80211_get_DA(hdr);

		return il4965_find_station(il, da);
	}
}

static inline u32
il4965_get_scd_ssn(struct il4965_tx_resp *tx_resp)
{
	return le32_to_cpup(&tx_resp->u.status +
			    tx_resp->frame_count) & IEEE80211_MAX_SN;
}

static inline u32
il4965_tx_status_to_mac80211(u32 status)
{
	status &= TX_STATUS_MSK;

	switch (status) {
	case TX_STATUS_SUCCESS:
	case TX_STATUS_DIRECT_DONE:
		return IEEE80211_TX_STAT_ACK;
	case TX_STATUS_FAIL_DEST_PS:
		return IEEE80211_TX_STAT_TX_FILTERED;
	default:
		return 0;
	}
}

/*
 * il4965_tx_status_reply_tx - Handle Tx response for frames in aggregation queue
 */
static int
il4965_tx_status_reply_tx(struct il_priv *il, struct il_ht_agg *agg,
			  struct il4965_tx_resp *tx_resp, int txq_id,
			  u16 start_idx)
{
	u16 status;
	struct agg_tx_status *frame_status = tx_resp->u.agg_status;
	struct ieee80211_tx_info *info = NULL;
	struct ieee80211_hdr *hdr = NULL;
	u32 rate_n_flags = le32_to_cpu(tx_resp->rate_n_flags);
	int i, sh, idx;
	u16 seq;
	if (agg->wait_for_ba)
		D_TX_REPLY("got tx response w/o block-ack\n");

	agg->frame_count = tx_resp->frame_count;
	agg->start_idx = start_idx;
	agg->rate_n_flags = rate_n_flags;
	agg->bitmap = 0;

	/* num frames attempted by Tx command */
	if (agg->frame_count == 1) {
		/* Only one frame was attempted; no block-ack will arrive */
		status = le16_to_cpu(frame_status[0].status);
		idx = start_idx;

		D_TX_REPLY("FrameCnt = %d, StartIdx=%d idx=%d\n",
			   agg->frame_count, agg->start_idx, idx);

		info = IEEE80211_SKB_CB(il->txq[txq_id].skbs[idx]);
		info->status.rates[0].count = tx_resp->failure_frame + 1;
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;
		info->flags |= il4965_tx_status_to_mac80211(status);
		il4965_hwrate_to_tx_control(il, rate_n_flags, info);

		D_TX_REPLY("1 Frame 0x%x failure :%d\n", status & 0xff,
			   tx_resp->failure_frame);
		D_TX_REPLY("Rate Info rate_n_flags=%x\n", rate_n_flags);

		agg->wait_for_ba = 0;
	} else {
		/* Two or more frames were attempted; expect block-ack */
		u64 bitmap = 0;
		int start = agg->start_idx;
		struct sk_buff *skb;

		/* Construct bit-map of pending frames within Tx win */
		for (i = 0; i < agg->frame_count; i++) {
			u16 sc;
			status = le16_to_cpu(frame_status[i].status);
			seq = le16_to_cpu(frame_status[i].sequence);
			idx = SEQ_TO_IDX(seq);
			txq_id = SEQ_TO_QUEUE(seq);

			if (status &
			    (AGG_TX_STATE_FEW_BYTES_MSK |
			     AGG_TX_STATE_ABORT_MSK))
				continue;

			D_TX_REPLY("FrameCnt = %d, txq_id=%d idx=%d\n",
				   agg->frame_count, txq_id, idx);

			skb = il->txq[txq_id].skbs[idx];
			if (WARN_ON_ONCE(skb == NULL))
				return -1;
			hdr = (struct ieee80211_hdr *) skb->data;

			sc = le16_to_cpu(hdr->seq_ctrl);
			if (idx != (IEEE80211_SEQ_TO_SN(sc) & 0xff)) {
				IL_ERR("BUG_ON idx doesn't match seq control"
				       " idx=%d, seq_idx=%d, seq=%d\n", idx,
				       IEEE80211_SEQ_TO_SN(sc), hdr->seq_ctrl);
				return -1;
			}

			D_TX_REPLY("AGG Frame i=%d idx %d seq=%d\n", i, idx,
				   IEEE80211_SEQ_TO_SN(sc));

			sh = idx - start;
			if (sh > 64) {
				sh = (start - idx) + 0xff;
				bitmap = bitmap << sh;
				sh = 0;
				start = idx;
			} else if (sh < -64)
				sh = 0xff - (start - idx);
			else if (sh < 0) {
				sh = start - idx;
				start = idx;
				bitmap = bitmap << sh;
				sh = 0;
			}
			bitmap |= 1ULL << sh;
			D_TX_REPLY("start=%d bitmap=0x%llx\n", start,
				   (unsigned long long)bitmap);
		}

		agg->bitmap = bitmap;
		agg->start_idx = start;
		D_TX_REPLY("Frames %d start_idx=%d bitmap=0x%llx\n",
			   agg->frame_count, agg->start_idx,
			   (unsigned long long)agg->bitmap);

		if (bitmap)
			agg->wait_for_ba = 1;
	}
	return 0;
}

/*
 * il4965_hdl_tx - Handle standard (non-aggregation) Tx response
 */
static void
il4965_hdl_tx(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	int idx = SEQ_TO_IDX(sequence);
	struct il_tx_queue *txq = &il->txq[txq_id];
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *info;
	struct il4965_tx_resp *tx_resp = (void *)&pkt->u.raw[0];
	u32 status = le32_to_cpu(tx_resp->u.status);
	int tid;
	int sta_id;
	int freed;
	u8 *qc = NULL;
	unsigned long flags;

	if (idx >= txq->q.n_bd || il_queue_used(&txq->q, idx) == 0) {
		IL_ERR("Read idx for DMA queue txq_id (%d) idx %d "
		       "is out of range [0-%d] %d %d\n", txq_id, idx,
		       txq->q.n_bd, txq->q.write_ptr, txq->q.read_ptr);
		return;
	}

	txq->time_stamp = jiffies;

	skb = txq->skbs[txq->q.read_ptr];
	info = IEEE80211_SKB_CB(skb);
	memset(&info->status, 0, sizeof(info->status));

	hdr = (struct ieee80211_hdr *) skb->data;
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & 0xf;
	}

	sta_id = il4965_get_ra_sta_id(il, hdr);
	if (txq->sched_retry && unlikely(sta_id == IL_INVALID_STATION)) {
		IL_ERR("Station not known\n");
		return;
	}

	/*
	 * Firmware will not transmit frame on passive channel, if it not yet
	 * received some valid frame on that channel. When this error happen
	 * we have to wait until firmware will unblock itself i.e. when we
	 * note received beacon or other frame. We unblock queues in
	 * il4965_pass_packet_to_mac80211 or in il_mac_bss_info_changed.
	 */
	if (unlikely((status & TX_STATUS_MSK) == TX_STATUS_FAIL_PASSIVE_NO_RX) &&
	    il->iw_mode == NL80211_IFTYPE_STATION) {
		il_stop_queues_by_reason(il, IL_STOP_REASON_PASSIVE);
		D_INFO("Stopped queues - RX waiting on passive channel\n");
	}

	spin_lock_irqsave(&il->sta_lock, flags);
	if (txq->sched_retry) {
		const u32 scd_ssn = il4965_get_scd_ssn(tx_resp);
		struct il_ht_agg *agg;

		if (WARN_ON(!qc))
			goto out;

		agg = &il->stations[sta_id].tid[tid].agg;

		il4965_tx_status_reply_tx(il, agg, tx_resp, txq_id, idx);

		/* check if BAR is needed */
		if (tx_resp->frame_count == 1 &&
		    !il4965_is_tx_success(status))
			info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;

		if (txq->q.read_ptr != (scd_ssn & 0xff)) {
			idx = il_queue_dec_wrap(scd_ssn & 0xff, txq->q.n_bd);
			D_TX_REPLY("Retry scheduler reclaim scd_ssn "
				   "%d idx %d\n", scd_ssn, idx);
			freed = il4965_tx_queue_reclaim(il, txq_id, idx);
			il4965_free_tfds_in_queue(il, sta_id, tid, freed);

			if (il->mac80211_registered &&
			    il_queue_space(&txq->q) > txq->q.low_mark &&
			    agg->state != IL_EMPTYING_HW_QUEUE_DELBA)
				il_wake_queue(il, txq);
		}
	} else {
		info->status.rates[0].count = tx_resp->failure_frame + 1;
		info->flags |= il4965_tx_status_to_mac80211(status);
		il4965_hwrate_to_tx_control(il,
					    le32_to_cpu(tx_resp->rate_n_flags),
					    info);

		D_TX_REPLY("TXQ %d status %s (0x%08x) "
			   "rate_n_flags 0x%x retries %d\n", txq_id,
			   il4965_get_tx_fail_reason(status), status,
			   le32_to_cpu(tx_resp->rate_n_flags),
			   tx_resp->failure_frame);

		freed = il4965_tx_queue_reclaim(il, txq_id, idx);
		if (qc && likely(sta_id != IL_INVALID_STATION))
			il4965_free_tfds_in_queue(il, sta_id, tid, freed);
		else if (sta_id == IL_INVALID_STATION)
			D_TX_REPLY("Station not known\n");

		if (il->mac80211_registered &&
		    il_queue_space(&txq->q) > txq->q.low_mark)
			il_wake_queue(il, txq);
	}
out:
	if (qc && likely(sta_id != IL_INVALID_STATION))
		il4965_txq_check_empty(il, sta_id, tid, txq_id);

	il4965_check_abort_status(il, tx_resp->frame_count, status);

	spin_unlock_irqrestore(&il->sta_lock, flags);
}

/*
 * translate ucode response to mac80211 tx status control values
 */
void
il4965_hwrate_to_tx_control(struct il_priv *il, u32 rate_n_flags,
			    struct ieee80211_tx_info *info)
{
	struct ieee80211_tx_rate *r = &info->status.rates[0];

	info->status.antenna =
	    ((rate_n_flags & RATE_MCS_ANT_ABC_MSK) >> RATE_MCS_ANT_POS);
	if (rate_n_flags & RATE_MCS_HT_MSK)
		r->flags |= IEEE80211_TX_RC_MCS;
	if (rate_n_flags & RATE_MCS_GF_MSK)
		r->flags |= IEEE80211_TX_RC_GREEN_FIELD;
	if (rate_n_flags & RATE_MCS_HT40_MSK)
		r->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
	if (rate_n_flags & RATE_MCS_DUP_MSK)
		r->flags |= IEEE80211_TX_RC_DUP_DATA;
	if (rate_n_flags & RATE_MCS_SGI_MSK)
		r->flags |= IEEE80211_TX_RC_SHORT_GI;
	r->idx = il4965_hwrate_to_mac80211_idx(rate_n_flags, info->band);
}

/*
 * il4965_hdl_compressed_ba - Handler for N_COMPRESSED_BA
 *
 * Handles block-acknowledge notification from device, which reports success
 * of frames sent via aggregation.
 */
static void
il4965_hdl_compressed_ba(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_compressed_ba_resp *ba_resp = &pkt->u.compressed_ba;
	struct il_tx_queue *txq = NULL;
	struct il_ht_agg *agg;
	int idx;
	int sta_id;
	int tid;
	unsigned long flags;

	/* "flow" corresponds to Tx queue */
	u16 scd_flow = le16_to_cpu(ba_resp->scd_flow);

	/* "ssn" is start of block-ack Tx win, corresponds to idx
	 * (in Tx queue's circular buffer) of first TFD/frame in win */
	u16 ba_resp_scd_ssn = le16_to_cpu(ba_resp->scd_ssn);

	if (scd_flow >= il->hw_params.max_txq_num) {
		IL_ERR("BUG_ON scd_flow is bigger than number of queues\n");
		return;
	}

	txq = &il->txq[scd_flow];
	sta_id = ba_resp->sta_id;
	tid = ba_resp->tid;
	agg = &il->stations[sta_id].tid[tid].agg;
	if (unlikely(agg->txq_id != scd_flow)) {
		/*
		 * FIXME: this is a uCode bug which need to be addressed,
		 * log the information and return for now!
		 * since it is possible happen very often and in order
		 * not to fill the syslog, don't enable the logging by default
		 */
		D_TX_REPLY("BA scd_flow %d does not match txq_id %d\n",
			   scd_flow, agg->txq_id);
		return;
	}

	/* Find idx just before block-ack win */
	idx = il_queue_dec_wrap(ba_resp_scd_ssn & 0xff, txq->q.n_bd);

	spin_lock_irqsave(&il->sta_lock, flags);

	D_TX_REPLY("N_COMPRESSED_BA [%d] Received from %pM, " "sta_id = %d\n",
		   agg->wait_for_ba, (u8 *) &ba_resp->sta_addr_lo32,
		   ba_resp->sta_id);
	D_TX_REPLY("TID = %d, SeqCtl = %d, bitmap = 0x%llx," "scd_flow = "
		   "%d, scd_ssn = %d\n", ba_resp->tid, ba_resp->seq_ctl,
		   (unsigned long long)le64_to_cpu(ba_resp->bitmap),
		   ba_resp->scd_flow, ba_resp->scd_ssn);
	D_TX_REPLY("DAT start_idx = %d, bitmap = 0x%llx\n", agg->start_idx,
		   (unsigned long long)agg->bitmap);

	/* Update driver's record of ACK vs. not for each frame in win */
	il4965_tx_status_reply_compressed_ba(il, agg, ba_resp);

	/* Release all TFDs before the SSN, i.e. all TFDs in front of
	 * block-ack win (we assume that they've been successfully
	 * transmitted ... if not, it's too late anyway). */
	if (txq->q.read_ptr != (ba_resp_scd_ssn & 0xff)) {
		/* calculate mac80211 ampdu sw queue to wake */
		int freed = il4965_tx_queue_reclaim(il, scd_flow, idx);
		il4965_free_tfds_in_queue(il, sta_id, tid, freed);

		if (il_queue_space(&txq->q) > txq->q.low_mark &&
		    il->mac80211_registered &&
		    agg->state != IL_EMPTYING_HW_QUEUE_DELBA)
			il_wake_queue(il, txq);

		il4965_txq_check_empty(il, sta_id, tid, scd_flow);
	}

	spin_unlock_irqrestore(&il->sta_lock, flags);
}

#ifdef CONFIG_IWLEGACY_DEBUG
const char *
il4965_get_tx_fail_reason(u32 status)
{
#define TX_STATUS_FAIL(x) case TX_STATUS_FAIL_ ## x: return #x
#define TX_STATUS_POSTPONE(x) case TX_STATUS_POSTPONE_ ## x: return #x

	switch (status & TX_STATUS_MSK) {
	case TX_STATUS_SUCCESS:
		return "SUCCESS";
		TX_STATUS_POSTPONE(DELAY);
		TX_STATUS_POSTPONE(FEW_BYTES);
		TX_STATUS_POSTPONE(QUIET_PERIOD);
		TX_STATUS_POSTPONE(CALC_TTAK);
		TX_STATUS_FAIL(INTERNAL_CROSSED_RETRY);
		TX_STATUS_FAIL(SHORT_LIMIT);
		TX_STATUS_FAIL(LONG_LIMIT);
		TX_STATUS_FAIL(FIFO_UNDERRUN);
		TX_STATUS_FAIL(DRAIN_FLOW);
		TX_STATUS_FAIL(RFKILL_FLUSH);
		TX_STATUS_FAIL(LIFE_EXPIRE);
		TX_STATUS_FAIL(DEST_PS);
		TX_STATUS_FAIL(HOST_ABORTED);
		TX_STATUS_FAIL(BT_RETRY);
		TX_STATUS_FAIL(STA_INVALID);
		TX_STATUS_FAIL(FRAG_DROPPED);
		TX_STATUS_FAIL(TID_DISABLE);
		TX_STATUS_FAIL(FIFO_FLUSHED);
		TX_STATUS_FAIL(INSUFFICIENT_CF_POLL);
		TX_STATUS_FAIL(PASSIVE_NO_RX);
		TX_STATUS_FAIL(NO_BEACON_ON_RADAR);
	}

	return "UNKNOWN";

#undef TX_STATUS_FAIL
#undef TX_STATUS_POSTPONE
}
#endif /* CONFIG_IWLEGACY_DEBUG */

static struct il_link_quality_cmd *
il4965_sta_alloc_lq(struct il_priv *il, u8 sta_id)
{
	int i, r;
	struct il_link_quality_cmd *link_cmd;
	u32 rate_flags = 0;
	__le32 rate_n_flags;

	link_cmd = kzalloc(sizeof(struct il_link_quality_cmd), GFP_KERNEL);
	if (!link_cmd) {
		IL_ERR("Unable to allocate memory for LQ cmd.\n");
		return NULL;
	}
	/* Set up the rate scaling to start at selected rate, fall back
	 * all the way down to 1M in IEEE order, and then spin on 1M */
	if (il->band == NL80211_BAND_5GHZ)
		r = RATE_6M_IDX;
	else
		r = RATE_1M_IDX;

	if (r >= IL_FIRST_CCK_RATE && r <= IL_LAST_CCK_RATE)
		rate_flags |= RATE_MCS_CCK_MSK;

	rate_flags |=
	    il4965_first_antenna(il->hw_params.
				 valid_tx_ant) << RATE_MCS_ANT_POS;
	rate_n_flags = cpu_to_le32(il_rates[r].plcp | rate_flags);
	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		link_cmd->rs_table[i].rate_n_flags = rate_n_flags;

	link_cmd->general_params.single_stream_ant_msk =
	    il4965_first_antenna(il->hw_params.valid_tx_ant);

	link_cmd->general_params.dual_stream_ant_msk =
	    il->hw_params.valid_tx_ant & ~il4965_first_antenna(il->hw_params.
							       valid_tx_ant);
	if (!link_cmd->general_params.dual_stream_ant_msk) {
		link_cmd->general_params.dual_stream_ant_msk = ANT_AB;
	} else if (il4965_num_of_ant(il->hw_params.valid_tx_ant) == 2) {
		link_cmd->general_params.dual_stream_ant_msk =
		    il->hw_params.valid_tx_ant;
	}

	link_cmd->agg_params.agg_dis_start_th = LINK_QUAL_AGG_DISABLE_START_DEF;
	link_cmd->agg_params.agg_time_limit =
	    cpu_to_le16(LINK_QUAL_AGG_TIME_LIMIT_DEF);

	link_cmd->sta_id = sta_id;

	return link_cmd;
}

/*
 * il4965_add_bssid_station - Add the special IBSS BSSID station
 *
 * Function sleeps.
 */
int
il4965_add_bssid_station(struct il_priv *il, const u8 *addr, u8 *sta_id_r)
{
	int ret;
	u8 sta_id;
	struct il_link_quality_cmd *link_cmd;
	unsigned long flags;

	if (sta_id_r)
		*sta_id_r = IL_INVALID_STATION;

	ret = il_add_station_common(il, addr, 0, NULL, &sta_id);
	if (ret) {
		IL_ERR("Unable to add station %pM\n", addr);
		return ret;
	}

	if (sta_id_r)
		*sta_id_r = sta_id;

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].used |= IL_STA_LOCAL;
	spin_unlock_irqrestore(&il->sta_lock, flags);

	/* Set up default rate scaling table in device's station table */
	link_cmd = il4965_sta_alloc_lq(il, sta_id);
	if (!link_cmd) {
		IL_ERR("Unable to initialize rate scaling for station %pM.\n",
		       addr);
		return -ENOMEM;
	}

	ret = il_send_lq_cmd(il, link_cmd, CMD_SYNC, true);
	if (ret)
		IL_ERR("Link quality command failed (%d)\n", ret);

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].lq = link_cmd;
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return 0;
}

static int
il4965_static_wepkey_cmd(struct il_priv *il, bool send_if_empty)
{
	int i;
	u8 buff[sizeof(struct il_wep_cmd) +
		sizeof(struct il_wep_key) * WEP_KEYS_MAX];
	struct il_wep_cmd *wep_cmd = (struct il_wep_cmd *)buff;
	size_t cmd_size = sizeof(struct il_wep_cmd);
	struct il_host_cmd cmd = {
		.id = C_WEPKEY,
		.data = wep_cmd,
		.flags = CMD_SYNC,
	};
	bool not_empty = false;

	might_sleep();

	memset(wep_cmd, 0,
	       cmd_size + (sizeof(struct il_wep_key) * WEP_KEYS_MAX));

	for (i = 0; i < WEP_KEYS_MAX; i++) {
		u8 key_size = il->_4965.wep_keys[i].key_size;

		wep_cmd->key[i].key_idx = i;
		if (key_size) {
			wep_cmd->key[i].key_offset = i;
			not_empty = true;
		} else
			wep_cmd->key[i].key_offset = WEP_INVALID_OFFSET;

		wep_cmd->key[i].key_size = key_size;
		memcpy(&wep_cmd->key[i].key[3], il->_4965.wep_keys[i].key, key_size);
	}

	wep_cmd->global_key_type = WEP_KEY_WEP_TYPE;
	wep_cmd->num_keys = WEP_KEYS_MAX;

	cmd_size += sizeof(struct il_wep_key) * WEP_KEYS_MAX;
	cmd.len = cmd_size;

	if (not_empty || send_if_empty)
		return il_send_cmd(il, &cmd);
	else
		return 0;
}

int
il4965_restore_default_wep_keys(struct il_priv *il)
{
	lockdep_assert_held(&il->mutex);

	return il4965_static_wepkey_cmd(il, false);
}

int
il4965_remove_default_wep_key(struct il_priv *il,
			      struct ieee80211_key_conf *keyconf)
{
	int ret;
	int idx = keyconf->keyidx;

	lockdep_assert_held(&il->mutex);

	D_WEP("Removing default WEP key: idx=%d\n", idx);

	memset(&il->_4965.wep_keys[idx], 0, sizeof(struct il_wep_key));
	if (il_is_rfkill(il)) {
		D_WEP("Not sending C_WEPKEY command due to RFKILL.\n");
		/* but keys in device are clear anyway so return success */
		return 0;
	}
	ret = il4965_static_wepkey_cmd(il, 1);
	D_WEP("Remove default WEP key: idx=%d ret=%d\n", idx, ret);

	return ret;
}

int
il4965_set_default_wep_key(struct il_priv *il,
			   struct ieee80211_key_conf *keyconf)
{
	int ret;
	int len = keyconf->keylen;
	int idx = keyconf->keyidx;

	lockdep_assert_held(&il->mutex);

	if (len != WEP_KEY_LEN_128 && len != WEP_KEY_LEN_64) {
		D_WEP("Bad WEP key length %d\n", keyconf->keylen);
		return -EINVAL;
	}

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->hw_key_idx = HW_KEY_DEFAULT;
	il->stations[IL_AP_ID].keyinfo.cipher = keyconf->cipher;

	il->_4965.wep_keys[idx].key_size = len;
	memcpy(&il->_4965.wep_keys[idx].key, &keyconf->key, len);

	ret = il4965_static_wepkey_cmd(il, false);

	D_WEP("Set default WEP key: len=%d idx=%d ret=%d\n", len, idx, ret);
	return ret;
}

static int
il4965_set_wep_dynamic_key_info(struct il_priv *il,
				struct ieee80211_key_conf *keyconf, u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;

	key_flags |= (STA_KEY_FLG_WEP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (keyconf->keylen == WEP_KEY_LEN_128)
		key_flags |= STA_KEY_FLG_KEY_SIZE_MSK;

	if (sta_id == il->hw_params.bcast_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	spin_lock_irqsave(&il->sta_lock, flags);

	il->stations[sta_id].keyinfo.cipher = keyconf->cipher;
	il->stations[sta_id].keyinfo.keylen = keyconf->keylen;
	il->stations[sta_id].keyinfo.keyidx = keyconf->keyidx;

	memcpy(il->stations[sta_id].keyinfo.key, keyconf->key, keyconf->keylen);

	memcpy(&il->stations[sta_id].sta.key.key[3], keyconf->key,
	       keyconf->keylen);

	if ((il->stations[sta_id].sta.key.
	     key_flags & STA_KEY_FLG_ENCRYPT_MSK) == STA_KEY_FLG_NO_ENC)
		il->stations[sta_id].sta.key.key_offset =
		    il_get_free_ucode_key_idx(il);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(il->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
	     "no space for a new key");

	il->stations[sta_id].sta.key.key_flags = key_flags;
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	memcpy(&sta_cmd, &il->stations[sta_id].sta,
	       sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

static int
il4965_set_ccmp_dynamic_key_info(struct il_priv *il,
				 struct ieee80211_key_conf *keyconf, u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	key_flags |= (STA_KEY_FLG_CCMP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == il->hw_params.bcast_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].keyinfo.cipher = keyconf->cipher;
	il->stations[sta_id].keyinfo.keylen = keyconf->keylen;

	memcpy(il->stations[sta_id].keyinfo.key, keyconf->key, keyconf->keylen);

	memcpy(il->stations[sta_id].sta.key.key, keyconf->key, keyconf->keylen);

	if ((il->stations[sta_id].sta.key.
	     key_flags & STA_KEY_FLG_ENCRYPT_MSK) == STA_KEY_FLG_NO_ENC)
		il->stations[sta_id].sta.key.key_offset =
		    il_get_free_ucode_key_idx(il);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(il->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
	     "no space for a new key");

	il->stations[sta_id].sta.key.key_flags = key_flags;
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	memcpy(&sta_cmd, &il->stations[sta_id].sta,
	       sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

static int
il4965_set_tkip_dynamic_key_info(struct il_priv *il,
				 struct ieee80211_key_conf *keyconf, u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;

	key_flags |= (STA_KEY_FLG_TKIP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == il->hw_params.bcast_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;

	spin_lock_irqsave(&il->sta_lock, flags);

	il->stations[sta_id].keyinfo.cipher = keyconf->cipher;
	il->stations[sta_id].keyinfo.keylen = 16;

	if ((il->stations[sta_id].sta.key.
	     key_flags & STA_KEY_FLG_ENCRYPT_MSK) == STA_KEY_FLG_NO_ENC)
		il->stations[sta_id].sta.key.key_offset =
		    il_get_free_ucode_key_idx(il);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(il->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
	     "no space for a new key");

	il->stations[sta_id].sta.key.key_flags = key_flags;

	/* This copy is acutally not needed: we get the key with each TX */
	memcpy(il->stations[sta_id].keyinfo.key, keyconf->key, 16);

	memcpy(il->stations[sta_id].sta.key.key, keyconf->key, 16);

	spin_unlock_irqrestore(&il->sta_lock, flags);

	return 0;
}

void
il4965_update_tkip_key(struct il_priv *il, struct ieee80211_key_conf *keyconf,
		       struct ieee80211_sta *sta, u32 iv32, u16 *phase1key)
{
	u8 sta_id;
	unsigned long flags;
	int i;

	if (il_scan_cancel(il)) {
		/* cancel scan failed, just live w/ bad key and rely
		   briefly on SW decryption */
		return;
	}

	sta_id = il_sta_id_or_broadcast(il, sta);
	if (sta_id == IL_INVALID_STATION)
		return;

	spin_lock_irqsave(&il->sta_lock, flags);

	il->stations[sta_id].sta.key.tkip_rx_tsc_byte2 = (u8) iv32;

	for (i = 0; i < 5; i++)
		il->stations[sta_id].sta.key.tkip_rx_ttak[i] =
		    cpu_to_le16(phase1key[i]);

	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	il_send_add_sta(il, &il->stations[sta_id].sta, CMD_ASYNC);

	spin_unlock_irqrestore(&il->sta_lock, flags);
}

int
il4965_remove_dynamic_key(struct il_priv *il,
			  struct ieee80211_key_conf *keyconf, u8 sta_id)
{
	unsigned long flags;
	u16 key_flags;
	u8 keyidx;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	il->_4965.key_mapping_keys--;

	spin_lock_irqsave(&il->sta_lock, flags);
	key_flags = le16_to_cpu(il->stations[sta_id].sta.key.key_flags);
	keyidx = (key_flags >> STA_KEY_FLG_KEYID_POS) & 0x3;

	D_WEP("Remove dynamic key: idx=%d sta=%d\n", keyconf->keyidx, sta_id);

	if (keyconf->keyidx != keyidx) {
		/* We need to remove a key with idx different that the one
		 * in the uCode. This means that the key we need to remove has
		 * been replaced by another one with different idx.
		 * Don't do anything and return ok
		 */
		spin_unlock_irqrestore(&il->sta_lock, flags);
		return 0;
	}

	if (il->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_INVALID) {
		IL_WARN("Removing wrong key %d 0x%x\n", keyconf->keyidx,
			key_flags);
		spin_unlock_irqrestore(&il->sta_lock, flags);
		return 0;
	}

	if (!test_and_clear_bit
	    (il->stations[sta_id].sta.key.key_offset, &il->ucode_key_table))
		IL_ERR("idx %d not used in uCode key table.\n",
		       il->stations[sta_id].sta.key.key_offset);
	memset(&il->stations[sta_id].keyinfo, 0, sizeof(struct il_hw_key));
	memset(&il->stations[sta_id].sta.key, 0, sizeof(struct il4965_keyinfo));
	il->stations[sta_id].sta.key.key_flags =
	    STA_KEY_FLG_NO_ENC | STA_KEY_FLG_INVALID;
	il->stations[sta_id].sta.key.key_offset = keyconf->hw_key_idx;
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	if (il_is_rfkill(il)) {
		D_WEP
		    ("Not sending C_ADD_STA command because RFKILL enabled.\n");
		spin_unlock_irqrestore(&il->sta_lock, flags);
		return 0;
	}
	memcpy(&sta_cmd, &il->stations[sta_id].sta,
	       sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

int
il4965_set_dynamic_key(struct il_priv *il, struct ieee80211_key_conf *keyconf,
		       u8 sta_id)
{
	int ret;

	lockdep_assert_held(&il->mutex);

	il->_4965.key_mapping_keys++;
	keyconf->hw_key_idx = HW_KEY_DYNAMIC;

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		ret =
		    il4965_set_ccmp_dynamic_key_info(il, keyconf, sta_id);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		ret =
		    il4965_set_tkip_dynamic_key_info(il, keyconf, sta_id);
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		ret = il4965_set_wep_dynamic_key_info(il, keyconf, sta_id);
		break;
	default:
		IL_ERR("Unknown alg: %s cipher = %x\n", __func__,
		       keyconf->cipher);
		ret = -EINVAL;
	}

	D_WEP("Set dynamic key: cipher=%x len=%d idx=%d sta=%d ret=%d\n",
	      keyconf->cipher, keyconf->keylen, keyconf->keyidx, sta_id, ret);

	return ret;
}

/*
 * il4965_alloc_bcast_station - add broadcast station into driver's station table.
 *
 * This adds the broadcast station into the driver's station table
 * and marks it driver active, so that it will be restored to the
 * device at the next best time.
 */
int
il4965_alloc_bcast_station(struct il_priv *il)
{
	struct il_link_quality_cmd *link_cmd;
	unsigned long flags;
	u8 sta_id;

	spin_lock_irqsave(&il->sta_lock, flags);
	sta_id = il_prep_station(il, il_bcast_addr, false, NULL);
	if (sta_id == IL_INVALID_STATION) {
		IL_ERR("Unable to prepare broadcast station\n");
		spin_unlock_irqrestore(&il->sta_lock, flags);

		return -EINVAL;
	}

	il->stations[sta_id].used |= IL_STA_DRIVER_ACTIVE;
	il->stations[sta_id].used |= IL_STA_BCAST;
	spin_unlock_irqrestore(&il->sta_lock, flags);

	link_cmd = il4965_sta_alloc_lq(il, sta_id);
	if (!link_cmd) {
		IL_ERR
		    ("Unable to initialize rate scaling for bcast station.\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].lq = link_cmd;
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return 0;
}

/*
 * il4965_update_bcast_station - update broadcast station's LQ command
 *
 * Only used by iwl4965. Placed here to have all bcast station management
 * code together.
 */
static int
il4965_update_bcast_station(struct il_priv *il)
{
	unsigned long flags;
	struct il_link_quality_cmd *link_cmd;
	u8 sta_id = il->hw_params.bcast_id;

	link_cmd = il4965_sta_alloc_lq(il, sta_id);
	if (!link_cmd) {
		IL_ERR("Unable to initialize rate scaling for bcast sta.\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&il->sta_lock, flags);
	if (il->stations[sta_id].lq)
		kfree(il->stations[sta_id].lq);
	else
		D_INFO("Bcast sta rate scaling has not been initialized.\n");
	il->stations[sta_id].lq = link_cmd;
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return 0;
}

int
il4965_update_bcast_stations(struct il_priv *il)
{
	return il4965_update_bcast_station(il);
}

/*
 * il4965_sta_tx_modify_enable_tid - Enable Tx for this TID in station table
 */
int
il4965_sta_tx_modify_enable_tid(struct il_priv *il, int sta_id, int tid)
{
	unsigned long flags;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	/* Remove "disable" flag, to enable Tx for this TID */
	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_TID_DISABLE_TX;
	il->stations[sta_id].sta.tid_disable_tx &= cpu_to_le16(~(1 << tid));
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &il->stations[sta_id].sta,
	       sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

int
il4965_sta_rx_agg_start(struct il_priv *il, struct ieee80211_sta *sta, int tid,
			u16 ssn)
{
	unsigned long flags;
	int sta_id;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	sta_id = il_sta_id(sta);
	if (sta_id == IL_INVALID_STATION)
		return -ENXIO;

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].sta.station_flags_msk = 0;
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_ADDBA_TID_MSK;
	il->stations[sta_id].sta.add_immediate_ba_tid = (u8) tid;
	il->stations[sta_id].sta.add_immediate_ba_ssn = cpu_to_le16(ssn);
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &il->stations[sta_id].sta,
	       sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

int
il4965_sta_rx_agg_stop(struct il_priv *il, struct ieee80211_sta *sta, int tid)
{
	unsigned long flags;
	int sta_id;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	sta_id = il_sta_id(sta);
	if (sta_id == IL_INVALID_STATION) {
		IL_ERR("Invalid station for AGG tid %d\n", tid);
		return -ENXIO;
	}

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].sta.station_flags_msk = 0;
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_DELBA_TID_MSK;
	il->stations[sta_id].sta.remove_immediate_ba_tid = (u8) tid;
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &il->stations[sta_id].sta,
	       sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

void
il4965_sta_modify_sleep_tx_count(struct il_priv *il, int sta_id, int cnt)
{
	unsigned long flags;

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].sta.station_flags |= STA_FLG_PWR_SAVE_MSK;
	il->stations[sta_id].sta.station_flags_msk = STA_FLG_PWR_SAVE_MSK;
	il->stations[sta_id].sta.sta.modify_mask =
	    STA_MODIFY_SLEEP_TX_COUNT_MSK;
	il->stations[sta_id].sta.sleep_tx_count = cpu_to_le16(cnt);
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	il_send_add_sta(il, &il->stations[sta_id].sta, CMD_ASYNC);
	spin_unlock_irqrestore(&il->sta_lock, flags);

}

void
il4965_update_chain_flags(struct il_priv *il)
{
	if (il->ops->set_rxon_chain) {
		il->ops->set_rxon_chain(il);
		if (il->active.rx_chain != il->staging.rx_chain)
			il_commit_rxon(il);
	}
}

static void
il4965_clear_free_frames(struct il_priv *il)
{
	struct list_head *element;

	D_INFO("%d frames on pre-allocated heap on clear.\n", il->frames_count);

	while (!list_empty(&il->free_frames)) {
		element = il->free_frames.next;
		list_del(element);
		kfree(list_entry(element, struct il_frame, list));
		il->frames_count--;
	}

	if (il->frames_count) {
		IL_WARN("%d frames still in use.  Did we lose one?\n",
			il->frames_count);
		il->frames_count = 0;
	}
}

static struct il_frame *
il4965_get_free_frame(struct il_priv *il)
{
	struct il_frame *frame;
	struct list_head *element;
	if (list_empty(&il->free_frames)) {
		frame = kzalloc(sizeof(*frame), GFP_KERNEL);
		if (!frame) {
			IL_ERR("Could not allocate frame!\n");
			return NULL;
		}

		il->frames_count++;
		return frame;
	}

	element = il->free_frames.next;
	list_del(element);
	return list_entry(element, struct il_frame, list);
}

static void
il4965_free_frame(struct il_priv *il, struct il_frame *frame)
{
	memset(frame, 0, sizeof(*frame));
	list_add(&frame->list, &il->free_frames);
}

static u32
il4965_fill_beacon_frame(struct il_priv *il, struct ieee80211_hdr *hdr,
			 int left)
{
	lockdep_assert_held(&il->mutex);

	if (!il->beacon_skb)
		return 0;

	if (il->beacon_skb->len > left)
		return 0;

	memcpy(hdr, il->beacon_skb->data, il->beacon_skb->len);

	return il->beacon_skb->len;
}

/* Parse the beacon frame to find the TIM element and set tim_idx & tim_size */
static void
il4965_set_beacon_tim(struct il_priv *il,
		      struct il_tx_beacon_cmd *tx_beacon_cmd, u8 * beacon,
		      u32 frame_size)
{
	u16 tim_idx;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)beacon;

	/*
	 * The idx is relative to frame start but we start looking at the
	 * variable-length part of the beacon.
	 */
	tim_idx = mgmt->u.beacon.variable - beacon;

	/* Parse variable-length elements of beacon to find WLAN_EID_TIM */
	while ((tim_idx < (frame_size - 2)) &&
	       (beacon[tim_idx] != WLAN_EID_TIM))
		tim_idx += beacon[tim_idx + 1] + 2;

	/* If TIM field was found, set variables */
	if ((tim_idx < (frame_size - 1)) && (beacon[tim_idx] == WLAN_EID_TIM)) {
		tx_beacon_cmd->tim_idx = cpu_to_le16(tim_idx);
		tx_beacon_cmd->tim_size = beacon[tim_idx + 1];
	} else
		IL_WARN("Unable to find TIM Element in beacon\n");
}

static unsigned int
il4965_hw_get_beacon_cmd(struct il_priv *il, struct il_frame *frame)
{
	struct il_tx_beacon_cmd *tx_beacon_cmd;
	u32 frame_size;
	u32 rate_flags;
	u32 rate;
	/*
	 * We have to set up the TX command, the TX Beacon command, and the
	 * beacon contents.
	 */

	lockdep_assert_held(&il->mutex);

	if (!il->beacon_enabled) {
		IL_ERR("Trying to build beacon without beaconing enabled\n");
		return 0;
	}

	/* Initialize memory */
	tx_beacon_cmd = &frame->u.beacon;
	memset(tx_beacon_cmd, 0, sizeof(*tx_beacon_cmd));

	/* Set up TX beacon contents */
	frame_size =
	    il4965_fill_beacon_frame(il, tx_beacon_cmd->frame,
				     sizeof(frame->u) - sizeof(*tx_beacon_cmd));
	if (WARN_ON_ONCE(frame_size > MAX_MPDU_SIZE))
		return 0;
	if (!frame_size)
		return 0;

	/* Set up TX command fields */
	tx_beacon_cmd->tx.len = cpu_to_le16((u16) frame_size);
	tx_beacon_cmd->tx.sta_id = il->hw_params.bcast_id;
	tx_beacon_cmd->tx.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;
	tx_beacon_cmd->tx.tx_flags =
	    TX_CMD_FLG_SEQ_CTL_MSK | TX_CMD_FLG_TSF_MSK |
	    TX_CMD_FLG_STA_RATE_MSK;

	/* Set up TX beacon command fields */
	il4965_set_beacon_tim(il, tx_beacon_cmd, (u8 *) tx_beacon_cmd->frame,
			      frame_size);

	/* Set up packet rate and flags */
	rate = il_get_lowest_plcp(il);
	il4965_toggle_tx_ant(il, &il->mgmt_tx_ant, il->hw_params.valid_tx_ant);
	rate_flags = BIT(il->mgmt_tx_ant) << RATE_MCS_ANT_POS;
	if ((rate >= IL_FIRST_CCK_RATE) && (rate <= IL_LAST_CCK_RATE))
		rate_flags |= RATE_MCS_CCK_MSK;
	tx_beacon_cmd->tx.rate_n_flags = cpu_to_le32(rate | rate_flags);

	return sizeof(*tx_beacon_cmd) + frame_size;
}

int
il4965_send_beacon_cmd(struct il_priv *il)
{
	struct il_frame *frame;
	unsigned int frame_size;
	int rc;

	frame = il4965_get_free_frame(il);
	if (!frame) {
		IL_ERR("Could not obtain free frame buffer for beacon "
		       "command.\n");
		return -ENOMEM;
	}

	frame_size = il4965_hw_get_beacon_cmd(il, frame);
	if (!frame_size) {
		IL_ERR("Error configuring the beacon command\n");
		il4965_free_frame(il, frame);
		return -EINVAL;
	}

	rc = il_send_cmd_pdu(il, C_TX_BEACON, frame_size, &frame->u.cmd[0]);

	il4965_free_frame(il, frame);

	return rc;
}

static inline dma_addr_t
il4965_tfd_tb_get_addr(struct il_tfd *tfd, u8 idx)
{
	struct il_tfd_tb *tb = &tfd->tbs[idx];

	dma_addr_t addr = get_unaligned_le32(&tb->lo);
	if (sizeof(dma_addr_t) > sizeof(u32))
		addr |=
		    ((dma_addr_t) (le16_to_cpu(tb->hi_n_len) & 0xF) << 16) <<
		    16;

	return addr;
}

static inline u16
il4965_tfd_tb_get_len(struct il_tfd *tfd, u8 idx)
{
	struct il_tfd_tb *tb = &tfd->tbs[idx];

	return le16_to_cpu(tb->hi_n_len) >> 4;
}

static inline void
il4965_tfd_set_tb(struct il_tfd *tfd, u8 idx, dma_addr_t addr, u16 len)
{
	struct il_tfd_tb *tb = &tfd->tbs[idx];
	u16 hi_n_len = len << 4;

	put_unaligned_le32(addr, &tb->lo);
	if (sizeof(dma_addr_t) > sizeof(u32))
		hi_n_len |= ((addr >> 16) >> 16) & 0xF;

	tb->hi_n_len = cpu_to_le16(hi_n_len);

	tfd->num_tbs = idx + 1;
}

static inline u8
il4965_tfd_get_num_tbs(struct il_tfd *tfd)
{
	return tfd->num_tbs & 0x1f;
}

/*
 * il4965_hw_txq_free_tfd - Free all chunks referenced by TFD [txq->q.read_ptr]
 *
 * Does NOT advance any TFD circular buffer read/write idxes
 * Does NOT free the TFD itself (which is within circular buffer)
 */
void
il4965_hw_txq_free_tfd(struct il_priv *il, struct il_tx_queue *txq)
{
	struct il_tfd *tfd_tmp = (struct il_tfd *)txq->tfds;
	struct il_tfd *tfd;
	struct pci_dev *dev = il->pci_dev;
	int idx = txq->q.read_ptr;
	int i;
	int num_tbs;

	tfd = &tfd_tmp[idx];

	/* Sanity check on number of chunks */
	num_tbs = il4965_tfd_get_num_tbs(tfd);

	if (num_tbs >= IL_NUM_OF_TBS) {
		IL_ERR("Too many chunks: %i\n", num_tbs);
		/* @todo issue fatal error, it is quite serious situation */
		return;
	}

	/* Unmap tx_cmd */
	if (num_tbs)
		dma_unmap_single(&dev->dev,
				 dma_unmap_addr(&txq->meta[idx], mapping),
				 dma_unmap_len(&txq->meta[idx], len),
				 DMA_BIDIRECTIONAL);

	/* Unmap chunks, if any. */
	for (i = 1; i < num_tbs; i++)
		dma_unmap_single(&dev->dev, il4965_tfd_tb_get_addr(tfd, i),
				 il4965_tfd_tb_get_len(tfd, i), DMA_TO_DEVICE);

	/* free SKB */
	if (txq->skbs) {
		struct sk_buff *skb = txq->skbs[txq->q.read_ptr];

		/* can be called from irqs-disabled context */
		if (skb) {
			dev_kfree_skb_any(skb);
			txq->skbs[txq->q.read_ptr] = NULL;
		}
	}
}

int
il4965_hw_txq_attach_buf_to_tfd(struct il_priv *il, struct il_tx_queue *txq,
				dma_addr_t addr, u16 len, u8 reset, u8 pad)
{
	struct il_queue *q;
	struct il_tfd *tfd, *tfd_tmp;
	u32 num_tbs;

	q = &txq->q;
	tfd_tmp = (struct il_tfd *)txq->tfds;
	tfd = &tfd_tmp[q->write_ptr];

	if (reset)
		memset(tfd, 0, sizeof(*tfd));

	num_tbs = il4965_tfd_get_num_tbs(tfd);

	/* Each TFD can point to a maximum 20 Tx buffers */
	if (num_tbs >= IL_NUM_OF_TBS) {
		IL_ERR("Error can not send more than %d chunks\n",
		       IL_NUM_OF_TBS);
		return -EINVAL;
	}

	BUG_ON(addr & ~DMA_BIT_MASK(36));
	if (unlikely(addr & ~IL_TX_DMA_MASK))
		IL_ERR("Unaligned address = %llx\n", (unsigned long long)addr);

	il4965_tfd_set_tb(tfd, num_tbs, addr, len);

	return 0;
}

/*
 * Tell nic where to find circular buffer of Tx Frame Descriptors for
 * given Tx queue, and enable the DMA channel used for that queue.
 *
 * 4965 supports up to 16 Tx queues in DRAM, mapped to up to 8 Tx DMA
 * channels supported in hardware.
 */
int
il4965_hw_tx_queue_init(struct il_priv *il, struct il_tx_queue *txq)
{
	int txq_id = txq->q.id;

	/* Circular buffer (TFD queue in DRAM) physical base address */
	il_wr(il, FH49_MEM_CBBC_QUEUE(txq_id), txq->q.dma_addr >> 8);

	return 0;
}

/******************************************************************************
 *
 * Generic RX handler implementations
 *
 ******************************************************************************/
static void
il4965_hdl_alive(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_alive_resp *palive;
	struct delayed_work *pwork;

	palive = &pkt->u.alive_frame;

	D_INFO("Alive ucode status 0x%08X revision " "0x%01X 0x%01X\n",
	       palive->is_valid, palive->ver_type, palive->ver_subtype);

	if (palive->ver_subtype == INITIALIZE_SUBTYPE) {
		D_INFO("Initialization Alive received.\n");
		memcpy(&il->card_alive_init, &pkt->u.alive_frame,
		       sizeof(struct il_init_alive_resp));
		pwork = &il->init_alive_start;
	} else {
		D_INFO("Runtime Alive received.\n");
		memcpy(&il->card_alive, &pkt->u.alive_frame,
		       sizeof(struct il_alive_resp));
		pwork = &il->alive_start;
	}

	/* We delay the ALIVE response by 5ms to
	 * give the HW RF Kill time to activate... */
	if (palive->is_valid == UCODE_VALID_OK)
		queue_delayed_work(il->workqueue, pwork, msecs_to_jiffies(5));
	else
		IL_WARN("uCode did not respond OK.\n");
}

/*
 * il4965_bg_stats_periodic - Timer callback to queue stats
 *
 * This callback is provided in order to send a stats request.
 *
 * This timer function is continually reset to execute within
 * 60 seconds since the last N_STATS was received.  We need to
 * ensure we receive the stats in order to update the temperature
 * used for calibrating the TXPOWER.
 */
static void
il4965_bg_stats_periodic(struct timer_list *t)
{
	struct il_priv *il = from_timer(il, t, stats_periodic);

	if (test_bit(S_EXIT_PENDING, &il->status))
		return;

	/* dont send host command if rf-kill is on */
	if (!il_is_ready_rf(il))
		return;

	il_send_stats_request(il, CMD_ASYNC, false);
}

static void
il4965_hdl_beacon(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il4965_beacon_notif *beacon =
	    (struct il4965_beacon_notif *)pkt->u.raw;
#ifdef CONFIG_IWLEGACY_DEBUG
	u8 rate = il4965_hw_get_rate(beacon->beacon_notify_hdr.rate_n_flags);

	D_RX("beacon status %x retries %d iss %d tsf:0x%.8x%.8x rate %d\n",
	     le32_to_cpu(beacon->beacon_notify_hdr.u.status) & TX_STATUS_MSK,
	     beacon->beacon_notify_hdr.failure_frame,
	     le32_to_cpu(beacon->ibss_mgr_status),
	     le32_to_cpu(beacon->high_tsf), le32_to_cpu(beacon->low_tsf), rate);
#endif
	il->ibss_manager = le32_to_cpu(beacon->ibss_mgr_status);
}

static void
il4965_perform_ct_kill_task(struct il_priv *il)
{
	unsigned long flags;

	D_POWER("Stop all queues\n");

	if (il->mac80211_registered)
		ieee80211_stop_queues(il->hw);

	_il_wr(il, CSR_UCODE_DRV_GP1_SET,
	       CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
	_il_rd(il, CSR_UCODE_DRV_GP1);

	spin_lock_irqsave(&il->reg_lock, flags);
	if (likely(_il_grab_nic_access(il)))
		_il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, flags);
}

/* Handle notification from uCode that card's power state is changing
 * due to software, hardware, or critical temperature RFKILL */
static void
il4965_hdl_card_state(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	u32 flags = le32_to_cpu(pkt->u.card_state_notif.flags);
	unsigned long status = il->status;

	D_RF_KILL("Card state received: HW:%s SW:%s CT:%s\n",
		  (flags & HW_CARD_DISABLED) ? "Kill" : "On",
		  (flags & SW_CARD_DISABLED) ? "Kill" : "On",
		  (flags & CT_CARD_DISABLED) ? "Reached" : "Not reached");

	if (flags & (SW_CARD_DISABLED | HW_CARD_DISABLED | CT_CARD_DISABLED)) {

		_il_wr(il, CSR_UCODE_DRV_GP1_SET,
		       CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

		il_wr(il, HBUS_TARG_MBX_C, HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED);

		if (!(flags & RXON_CARD_DISABLED)) {
			_il_wr(il, CSR_UCODE_DRV_GP1_CLR,
			       CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);
			il_wr(il, HBUS_TARG_MBX_C,
			      HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED);
		}
	}

	if (flags & CT_CARD_DISABLED)
		il4965_perform_ct_kill_task(il);

	if (flags & HW_CARD_DISABLED)
		set_bit(S_RFKILL, &il->status);
	else
		clear_bit(S_RFKILL, &il->status);

	if (!(flags & RXON_CARD_DISABLED))
		il_scan_cancel(il);

	if ((test_bit(S_RFKILL, &status) !=
	     test_bit(S_RFKILL, &il->status)))
		wiphy_rfkill_set_hw_state(il->hw->wiphy,
					  test_bit(S_RFKILL, &il->status));
	else
		wake_up(&il->wait_command_queue);
}

/*
 * il4965_setup_handlers - Initialize Rx handler callbacks
 *
 * Setup the RX handlers for each of the reply types sent from the uCode
 * to the host.
 *
 * This function chains into the hardware specific files for them to setup
 * any hardware specific handlers as well.
 */
static void
il4965_setup_handlers(struct il_priv *il)
{
	il->handlers[N_ALIVE] = il4965_hdl_alive;
	il->handlers[N_ERROR] = il_hdl_error;
	il->handlers[N_CHANNEL_SWITCH] = il_hdl_csa;
	il->handlers[N_SPECTRUM_MEASUREMENT] = il_hdl_spectrum_measurement;
	il->handlers[N_PM_SLEEP] = il_hdl_pm_sleep;
	il->handlers[N_PM_DEBUG_STATS] = il_hdl_pm_debug_stats;
	il->handlers[N_BEACON] = il4965_hdl_beacon;

	/*
	 * The same handler is used for both the REPLY to a discrete
	 * stats request from the host as well as for the periodic
	 * stats notifications (after received beacons) from the uCode.
	 */
	il->handlers[C_STATS] = il4965_hdl_c_stats;
	il->handlers[N_STATS] = il4965_hdl_stats;

	il_setup_rx_scan_handlers(il);

	/* status change handler */
	il->handlers[N_CARD_STATE] = il4965_hdl_card_state;

	il->handlers[N_MISSED_BEACONS] = il4965_hdl_missed_beacon;
	/* Rx handlers */
	il->handlers[N_RX_PHY] = il4965_hdl_rx_phy;
	il->handlers[N_RX_MPDU] = il4965_hdl_rx;
	il->handlers[N_RX] = il4965_hdl_rx;
	/* block ack */
	il->handlers[N_COMPRESSED_BA] = il4965_hdl_compressed_ba;
	/* Tx response */
	il->handlers[C_TX] = il4965_hdl_tx;
}

/*
 * il4965_rx_handle - Main entry function for receiving responses from uCode
 *
 * Uses the il->handlers callback function array to invoke
 * the appropriate handlers, including command responses,
 * frame-received notifications, and other notifications.
 */
void
il4965_rx_handle(struct il_priv *il)
{
	struct il_rx_buf *rxb;
	struct il_rx_pkt *pkt;
	struct il_rx_queue *rxq = &il->rxq;
	u32 r, i;
	int reclaim;
	unsigned long flags;
	u8 fill_rx = 0;
	u32 count = 8;
	int total_empty;

	/* uCode's read idx (stored in shared DRAM) indicates the last Rx
	 * buffer that the driver may process (last buffer filled by ucode). */
	r = le16_to_cpu(rxq->rb_stts->closed_rb_num) & 0x0FFF;
	i = rxq->read;

	/* Rx interrupt, but nothing sent from uCode */
	if (i == r)
		D_RX("r = %d, i = %d\n", r, i);

	/* calculate total frames need to be restock after handling RX */
	total_empty = r - rxq->write_actual;
	if (total_empty < 0)
		total_empty += RX_QUEUE_SIZE;

	if (total_empty > (RX_QUEUE_SIZE / 2))
		fill_rx = 1;

	while (i != r) {
		int len;

		rxb = rxq->queue[i];

		/* If an RXB doesn't have a Rx queue slot associated with it,
		 * then a bug has been introduced in the queue refilling
		 * routines -- catch it here */
		BUG_ON(rxb == NULL);

		rxq->queue[i] = NULL;

		dma_unmap_page(&il->pci_dev->dev, rxb->page_dma,
			       PAGE_SIZE << il->hw_params.rx_page_order,
			       DMA_FROM_DEVICE);
		pkt = rxb_addr(rxb);

		len = le32_to_cpu(pkt->len_n_flags) & IL_RX_FRAME_SIZE_MSK;
		len += sizeof(u32);	/* account for status word */

		reclaim = il_need_reclaim(il, pkt);

		/* Based on type of command response or notification,
		 *   handle those that need handling via function in
		 *   handlers table.  See il4965_setup_handlers() */
		if (il->handlers[pkt->hdr.cmd]) {
			D_RX("r = %d, i = %d, %s, 0x%02x\n", r, i,
			     il_get_cmd_string(pkt->hdr.cmd), pkt->hdr.cmd);
			il->isr_stats.handlers[pkt->hdr.cmd]++;
			il->handlers[pkt->hdr.cmd] (il, rxb);
		} else {
			/* No handling needed */
			D_RX("r %d i %d No handler needed for %s, 0x%02x\n", r,
			     i, il_get_cmd_string(pkt->hdr.cmd), pkt->hdr.cmd);
		}

		/*
		 * XXX: After here, we should always check rxb->page
		 * against NULL before touching it or its virtual
		 * memory (pkt). Because some handler might have
		 * already taken or freed the pages.
		 */

		if (reclaim) {
			/* Invoke any callbacks, transfer the buffer to caller,
			 * and fire off the (possibly) blocking il_send_cmd()
			 * as we reclaim the driver command queue */
			if (rxb->page)
				il_tx_cmd_complete(il, rxb);
			else
				IL_WARN("Claim null rxb?\n");
		}

		/* Reuse the page if possible. For notification packets and
		 * SKBs that fail to Rx correctly, add them back into the
		 * rx_free list for reuse later. */
		spin_lock_irqsave(&rxq->lock, flags);
		if (rxb->page != NULL) {
			rxb->page_dma =
			    dma_map_page(&il->pci_dev->dev, rxb->page, 0,
					 PAGE_SIZE << il->hw_params.rx_page_order,
					 DMA_FROM_DEVICE);

			if (unlikely(dma_mapping_error(&il->pci_dev->dev,
						       rxb->page_dma))) {
				__il_free_pages(il, rxb->page);
				rxb->page = NULL;
				list_add_tail(&rxb->list, &rxq->rx_used);
			} else {
				list_add_tail(&rxb->list, &rxq->rx_free);
				rxq->free_count++;
			}
		} else
			list_add_tail(&rxb->list, &rxq->rx_used);

		spin_unlock_irqrestore(&rxq->lock, flags);

		i = (i + 1) & RX_QUEUE_MASK;
		/* If there are a lot of unused frames,
		 * restock the Rx queue so ucode wont assert. */
		if (fill_rx) {
			count++;
			if (count >= 8) {
				rxq->read = i;
				il4965_rx_replenish_now(il);
				count = 0;
			}
		}
	}

	/* Backtrack one entry */
	rxq->read = i;
	if (fill_rx)
		il4965_rx_replenish_now(il);
	else
		il4965_rx_queue_restock(il);
}

/* call this function to flush any scheduled tasklet */
static inline void
il4965_synchronize_irq(struct il_priv *il)
{
	/* wait to make sure we flush pending tasklet */
	synchronize_irq(il->pci_dev->irq);
	tasklet_kill(&il->irq_tasklet);
}

static void
il4965_irq_tasklet(struct tasklet_struct *t)
{
	struct il_priv *il = from_tasklet(il, t, irq_tasklet);
	u32 inta, handled = 0;
	u32 inta_fh;
	unsigned long flags;
	u32 i;
#ifdef CONFIG_IWLEGACY_DEBUG
	u32 inta_mask;
#endif

	spin_lock_irqsave(&il->lock, flags);

	/* Ack/clear/reset pending uCode interrupts.
	 * Note:  Some bits in CSR_INT are "OR" of bits in CSR_FH_INT_STATUS,
	 *  and will clear only when CSR_FH_INT_STATUS gets cleared. */
	inta = _il_rd(il, CSR_INT);
	_il_wr(il, CSR_INT, inta);

	/* Ack/clear/reset pending flow-handler (DMA) interrupts.
	 * Any new interrupts that happen after this, either while we're
	 * in this tasklet, or later, will show up in next ISR/tasklet. */
	inta_fh = _il_rd(il, CSR_FH_INT_STATUS);
	_il_wr(il, CSR_FH_INT_STATUS, inta_fh);

#ifdef CONFIG_IWLEGACY_DEBUG
	if (il_get_debug_level(il) & IL_DL_ISR) {
		/* just for debug */
		inta_mask = _il_rd(il, CSR_INT_MASK);
		D_ISR("inta 0x%08x, enabled 0x%08x, fh 0x%08x\n", inta,
		      inta_mask, inta_fh);
	}
#endif

	spin_unlock_irqrestore(&il->lock, flags);

	/* Since CSR_INT and CSR_FH_INT_STATUS reads and clears are not
	 * atomic, make sure that inta covers all the interrupts that
	 * we've discovered, even if FH interrupt came in just after
	 * reading CSR_INT. */
	if (inta_fh & CSR49_FH_INT_RX_MASK)
		inta |= CSR_INT_BIT_FH_RX;
	if (inta_fh & CSR49_FH_INT_TX_MASK)
		inta |= CSR_INT_BIT_FH_TX;

	/* Now service all interrupt bits discovered above. */
	if (inta & CSR_INT_BIT_HW_ERR) {
		IL_ERR("Hardware error detected.  Restarting.\n");

		/* Tell the device to stop sending interrupts */
		il_disable_interrupts(il);

		il->isr_stats.hw++;
		il_irq_handle_error(il);

		handled |= CSR_INT_BIT_HW_ERR;

		return;
	}
#ifdef CONFIG_IWLEGACY_DEBUG
	if (il_get_debug_level(il) & (IL_DL_ISR)) {
		/* NIC fires this, but we don't use it, redundant with WAKEUP */
		if (inta & CSR_INT_BIT_SCD) {
			D_ISR("Scheduler finished to transmit "
			      "the frame/frames.\n");
			il->isr_stats.sch++;
		}

		/* Alive notification via Rx interrupt will do the real work */
		if (inta & CSR_INT_BIT_ALIVE) {
			D_ISR("Alive interrupt\n");
			il->isr_stats.alive++;
		}
	}
#endif
	/* Safely ignore these bits for debug checks below */
	inta &= ~(CSR_INT_BIT_SCD | CSR_INT_BIT_ALIVE);

	/* HW RF KILL switch toggled */
	if (inta & CSR_INT_BIT_RF_KILL) {
		int hw_rf_kill = 0;

		if (!(_il_rd(il, CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW))
			hw_rf_kill = 1;

		IL_WARN("RF_KILL bit toggled to %s.\n",
			hw_rf_kill ? "disable radio" : "enable radio");

		il->isr_stats.rfkill++;

		/* driver only loads ucode once setting the interface up.
		 * the driver allows loading the ucode even if the radio
		 * is killed. Hence update the killswitch state here. The
		 * rfkill handler will care about restarting if needed.
		 */
		if (hw_rf_kill) {
			set_bit(S_RFKILL, &il->status);
		} else {
			clear_bit(S_RFKILL, &il->status);
			il_force_reset(il, true);
		}
		wiphy_rfkill_set_hw_state(il->hw->wiphy, hw_rf_kill);

		handled |= CSR_INT_BIT_RF_KILL;
	}

	/* Chip got too hot and stopped itself */
	if (inta & CSR_INT_BIT_CT_KILL) {
		IL_ERR("Microcode CT kill error detected.\n");
		il->isr_stats.ctkill++;
		handled |= CSR_INT_BIT_CT_KILL;
	}

	/* Error detected by uCode */
	if (inta & CSR_INT_BIT_SW_ERR) {
		IL_ERR("Microcode SW error detected. " " Restarting 0x%X.\n",
		       inta);
		il->isr_stats.sw++;
		il_irq_handle_error(il);
		handled |= CSR_INT_BIT_SW_ERR;
	}

	/*
	 * uCode wakes up after power-down sleep.
	 * Tell device about any new tx or host commands enqueued,
	 * and about any Rx buffers made available while asleep.
	 */
	if (inta & CSR_INT_BIT_WAKEUP) {
		D_ISR("Wakeup interrupt\n");
		il_rx_queue_update_write_ptr(il, &il->rxq);
		for (i = 0; i < il->hw_params.max_txq_num; i++)
			il_txq_update_write_ptr(il, &il->txq[i]);
		il->isr_stats.wakeup++;
		handled |= CSR_INT_BIT_WAKEUP;
	}

	/* All uCode command responses, including Tx command responses,
	 * Rx "responses" (frame-received notification), and other
	 * notifications from uCode come through here*/
	if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX)) {
		il4965_rx_handle(il);
		il->isr_stats.rx++;
		handled |= (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX);
	}

	/* This "Tx" DMA channel is used only for loading uCode */
	if (inta & CSR_INT_BIT_FH_TX) {
		D_ISR("uCode load interrupt\n");
		il->isr_stats.tx++;
		handled |= CSR_INT_BIT_FH_TX;
		/* Wake up uCode load routine, now that load is complete */
		il->ucode_write_complete = 1;
		wake_up(&il->wait_command_queue);
	}

	if (inta & ~handled) {
		IL_ERR("Unhandled INTA bits 0x%08x\n", inta & ~handled);
		il->isr_stats.unhandled++;
	}

	if (inta & ~(il->inta_mask)) {
		IL_WARN("Disabled INTA bits 0x%08x were pending\n",
			inta & ~il->inta_mask);
		IL_WARN("   with FH49_INT = 0x%08x\n", inta_fh);
	}

	/* Re-enable all interrupts */
	/* only Re-enable if disabled by irq */
	if (test_bit(S_INT_ENABLED, &il->status))
		il_enable_interrupts(il);
	/* Re-enable RF_KILL if it occurred */
	else if (handled & CSR_INT_BIT_RF_KILL)
		il_enable_rfkill_int(il);

#ifdef CONFIG_IWLEGACY_DEBUG
	if (il_get_debug_level(il) & (IL_DL_ISR)) {
		inta = _il_rd(il, CSR_INT);
		inta_mask = _il_rd(il, CSR_INT_MASK);
		inta_fh = _il_rd(il, CSR_FH_INT_STATUS);
		D_ISR("End inta 0x%08x, enabled 0x%08x, fh 0x%08x, "
		      "flags 0x%08lx\n", inta, inta_mask, inta_fh, flags);
	}
#endif
}

/*****************************************************************************
 *
 * sysfs attributes
 *
 *****************************************************************************/

#ifdef CONFIG_IWLEGACY_DEBUG

/*
 * The following adds a new attribute to the sysfs representation
 * of this device driver (i.e. a new file in /sys/class/net/wlan0/device/)
 * used for controlling the debug level.
 *
 * See the level definitions in iwl for details.
 *
 * The debug_level being managed using sysfs below is a per device debug
 * level that is used instead of the global debug level if it (the per
 * device debug level) is set.
 */
static ssize_t
il4965_show_debug_level(struct device *d, struct device_attribute *attr,
			char *buf)
{
	struct il_priv *il = dev_get_drvdata(d);
	return sprintf(buf, "0x%08X\n", il_get_debug_level(il));
}

static ssize_t
il4965_store_debug_level(struct device *d, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct il_priv *il = dev_get_drvdata(d);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		IL_ERR("%s is not in hex or decimal form.\n", buf);
	else
		il->debug_level = val;

	return strnlen(buf, count);
}

static DEVICE_ATTR(debug_level, 0644, il4965_show_debug_level,
		   il4965_store_debug_level);

#endif /* CONFIG_IWLEGACY_DEBUG */

static ssize_t
il4965_show_temperature(struct device *d, struct device_attribute *attr,
			char *buf)
{
	struct il_priv *il = dev_get_drvdata(d);

	if (!il_is_alive(il))
		return -EAGAIN;

	return sprintf(buf, "%d\n", il->temperature);
}

static DEVICE_ATTR(temperature, 0444, il4965_show_temperature, NULL);

static ssize_t
il4965_show_tx_power(struct device *d, struct device_attribute *attr, char *buf)
{
	struct il_priv *il = dev_get_drvdata(d);

	if (!il_is_ready_rf(il))
		return sprintf(buf, "off\n");
	else
		return sprintf(buf, "%d\n", il->tx_power_user_lmt);
}

static ssize_t
il4965_store_tx_power(struct device *d, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct il_priv *il = dev_get_drvdata(d);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		IL_INFO("%s is not in decimal form.\n", buf);
	else {
		ret = il_set_tx_power(il, val, false);
		if (ret)
			IL_ERR("failed setting tx power (0x%08x).\n", ret);
		else
			ret = count;
	}
	return ret;
}

static DEVICE_ATTR(tx_power, 0644, il4965_show_tx_power,
		   il4965_store_tx_power);

static struct attribute *il_sysfs_entries[] = {
	&dev_attr_temperature.attr,
	&dev_attr_tx_power.attr,
#ifdef CONFIG_IWLEGACY_DEBUG
	&dev_attr_debug_level.attr,
#endif
	NULL
};

static const struct attribute_group il_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = il_sysfs_entries,
};

/******************************************************************************
 *
 * uCode download functions
 *
 ******************************************************************************/

static void
il4965_dealloc_ucode_pci(struct il_priv *il)
{
	il_free_fw_desc(il->pci_dev, &il->ucode_code);
	il_free_fw_desc(il->pci_dev, &il->ucode_data);
	il_free_fw_desc(il->pci_dev, &il->ucode_data_backup);
	il_free_fw_desc(il->pci_dev, &il->ucode_init);
	il_free_fw_desc(il->pci_dev, &il->ucode_init_data);
	il_free_fw_desc(il->pci_dev, &il->ucode_boot);
}

static void
il4965_nic_start(struct il_priv *il)
{
	/* Remove all resets to allow NIC to operate */
	_il_wr(il, CSR_RESET, 0);
}

static void il4965_ucode_callback(const struct firmware *ucode_raw,
				  void *context);
static int il4965_mac_setup_register(struct il_priv *il, u32 max_probe_length);

static int __must_check
il4965_request_firmware(struct il_priv *il, bool first)
{
	const char *name_pre = il->cfg->fw_name_pre;
	char tag[8];

	if (first) {
		il->fw_idx = il->cfg->ucode_api_max;
		sprintf(tag, "%d", il->fw_idx);
	} else {
		il->fw_idx--;
		sprintf(tag, "%d", il->fw_idx);
	}

	if (il->fw_idx < il->cfg->ucode_api_min) {
		IL_ERR("no suitable firmware found!\n");
		return -ENOENT;
	}

	sprintf(il->firmware_name, "%s%s%s", name_pre, tag, ".ucode");

	D_INFO("attempting to load firmware '%s'\n", il->firmware_name);

	return request_firmware_nowait(THIS_MODULE, 1, il->firmware_name,
				       &il->pci_dev->dev, GFP_KERNEL, il,
				       il4965_ucode_callback);
}

struct il4965_firmware_pieces {
	const void *inst, *data, *init, *init_data, *boot;
	size_t inst_size, data_size, init_size, init_data_size, boot_size;
};

static int
il4965_load_firmware(struct il_priv *il, const struct firmware *ucode_raw,
		     struct il4965_firmware_pieces *pieces)
{
	struct il_ucode_header *ucode = (void *)ucode_raw->data;
	u32 api_ver, hdr_size;
	const u8 *src;

	il->ucode_ver = le32_to_cpu(ucode->ver);
	api_ver = IL_UCODE_API(il->ucode_ver);

	switch (api_ver) {
	default:
	case 0:
	case 1:
	case 2:
		hdr_size = 24;
		if (ucode_raw->size < hdr_size) {
			IL_ERR("File size too small!\n");
			return -EINVAL;
		}
		pieces->inst_size = le32_to_cpu(ucode->v1.inst_size);
		pieces->data_size = le32_to_cpu(ucode->v1.data_size);
		pieces->init_size = le32_to_cpu(ucode->v1.init_size);
		pieces->init_data_size = le32_to_cpu(ucode->v1.init_data_size);
		pieces->boot_size = le32_to_cpu(ucode->v1.boot_size);
		src = ucode->v1.data;
		break;
	}

	/* Verify size of file vs. image size info in file's header */
	if (ucode_raw->size !=
	    hdr_size + pieces->inst_size + pieces->data_size +
	    pieces->init_size + pieces->init_data_size + pieces->boot_size) {

		IL_ERR("uCode file size %d does not match expected size\n",
		       (int)ucode_raw->size);
		return -EINVAL;
	}

	pieces->inst = src;
	src += pieces->inst_size;
	pieces->data = src;
	src += pieces->data_size;
	pieces->init = src;
	src += pieces->init_size;
	pieces->init_data = src;
	src += pieces->init_data_size;
	pieces->boot = src;
	src += pieces->boot_size;

	return 0;
}

/*
 * il4965_ucode_callback - callback when firmware was loaded
 *
 * If loaded successfully, copies the firmware into buffers
 * for the card to fetch (via DMA).
 */
static void
il4965_ucode_callback(const struct firmware *ucode_raw, void *context)
{
	struct il_priv *il = context;
	int err;
	struct il4965_firmware_pieces pieces;
	const unsigned int api_max = il->cfg->ucode_api_max;
	const unsigned int api_min = il->cfg->ucode_api_min;
	u32 api_ver;

	u32 max_probe_length = 200;
	u32 standard_phy_calibration_size =
	    IL_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE;

	memset(&pieces, 0, sizeof(pieces));

	if (!ucode_raw) {
		if (il->fw_idx <= il->cfg->ucode_api_max)
			IL_ERR("request for firmware file '%s' failed.\n",
			       il->firmware_name);
		goto try_again;
	}

	D_INFO("Loaded firmware file '%s' (%zd bytes).\n", il->firmware_name,
	       ucode_raw->size);

	/* Make sure that we got at least the API version number */
	if (ucode_raw->size < 4) {
		IL_ERR("File size way too small!\n");
		goto try_again;
	}

	/* Data from ucode file:  header followed by uCode images */
	err = il4965_load_firmware(il, ucode_raw, &pieces);

	if (err)
		goto try_again;

	api_ver = IL_UCODE_API(il->ucode_ver);

	/*
	 * api_ver should match the api version forming part of the
	 * firmware filename ... but we don't check for that and only rely
	 * on the API version read from firmware header from here on forward
	 */
	if (api_ver < api_min || api_ver > api_max) {
		IL_ERR("Driver unable to support your firmware API. "
		       "Driver supports v%u, firmware is v%u.\n", api_max,
		       api_ver);
		goto try_again;
	}

	if (api_ver != api_max)
		IL_ERR("Firmware has old API version. Expected v%u, "
		       "got v%u. New firmware can be obtained "
		       "from http://www.intellinuxwireless.org.\n", api_max,
		       api_ver);

	IL_INFO("loaded firmware version %u.%u.%u.%u\n",
		IL_UCODE_MAJOR(il->ucode_ver), IL_UCODE_MINOR(il->ucode_ver),
		IL_UCODE_API(il->ucode_ver), IL_UCODE_SERIAL(il->ucode_ver));

	snprintf(il->hw->wiphy->fw_version, sizeof(il->hw->wiphy->fw_version),
		 "%u.%u.%u.%u", IL_UCODE_MAJOR(il->ucode_ver),
		 IL_UCODE_MINOR(il->ucode_ver), IL_UCODE_API(il->ucode_ver),
		 IL_UCODE_SERIAL(il->ucode_ver));

	/*
	 * For any of the failures below (before allocating pci memory)
	 * we will try to load a version with a smaller API -- maybe the
	 * user just got a corrupted version of the latest API.
	 */

	D_INFO("f/w package hdr ucode version raw = 0x%x\n", il->ucode_ver);
	D_INFO("f/w package hdr runtime inst size = %zd\n", pieces.inst_size);
	D_INFO("f/w package hdr runtime data size = %zd\n", pieces.data_size);
	D_INFO("f/w package hdr init inst size = %zd\n", pieces.init_size);
	D_INFO("f/w package hdr init data size = %zd\n", pieces.init_data_size);
	D_INFO("f/w package hdr boot inst size = %zd\n", pieces.boot_size);

	/* Verify that uCode images will fit in card's SRAM */
	if (pieces.inst_size > il->hw_params.max_inst_size) {
		IL_ERR("uCode instr len %zd too large to fit in\n",
		       pieces.inst_size);
		goto try_again;
	}

	if (pieces.data_size > il->hw_params.max_data_size) {
		IL_ERR("uCode data len %zd too large to fit in\n",
		       pieces.data_size);
		goto try_again;
	}

	if (pieces.init_size > il->hw_params.max_inst_size) {
		IL_ERR("uCode init instr len %zd too large to fit in\n",
		       pieces.init_size);
		goto try_again;
	}

	if (pieces.init_data_size > il->hw_params.max_data_size) {
		IL_ERR("uCode init data len %zd too large to fit in\n",
		       pieces.init_data_size);
		goto try_again;
	}

	if (pieces.boot_size > il->hw_params.max_bsm_size) {
		IL_ERR("uCode boot instr len %zd too large to fit in\n",
		       pieces.boot_size);
		goto try_again;
	}

	/* Allocate ucode buffers for card's bus-master loading ... */

	/* Runtime instructions and 2 copies of data:
	 * 1) unmodified from disk
	 * 2) backup cache for save/restore during power-downs */
	il->ucode_code.len = pieces.inst_size;
	il_alloc_fw_desc(il->pci_dev, &il->ucode_code);

	il->ucode_data.len = pieces.data_size;
	il_alloc_fw_desc(il->pci_dev, &il->ucode_data);

	il->ucode_data_backup.len = pieces.data_size;
	il_alloc_fw_desc(il->pci_dev, &il->ucode_data_backup);

	if (!il->ucode_code.v_addr || !il->ucode_data.v_addr ||
	    !il->ucode_data_backup.v_addr)
		goto err_pci_alloc;

	/* Initialization instructions and data */
	if (pieces.init_size && pieces.init_data_size) {
		il->ucode_init.len = pieces.init_size;
		il_alloc_fw_desc(il->pci_dev, &il->ucode_init);

		il->ucode_init_data.len = pieces.init_data_size;
		il_alloc_fw_desc(il->pci_dev, &il->ucode_init_data);

		if (!il->ucode_init.v_addr || !il->ucode_init_data.v_addr)
			goto err_pci_alloc;
	}

	/* Bootstrap (instructions only, no data) */
	if (pieces.boot_size) {
		il->ucode_boot.len = pieces.boot_size;
		il_alloc_fw_desc(il->pci_dev, &il->ucode_boot);

		if (!il->ucode_boot.v_addr)
			goto err_pci_alloc;
	}

	/* Now that we can no longer fail, copy information */

	il->sta_key_max_num = STA_KEY_MAX_NUM;

	/* Copy images into buffers for card's bus-master reads ... */

	/* Runtime instructions (first block of data in file) */
	D_INFO("Copying (but not loading) uCode instr len %zd\n",
	       pieces.inst_size);
	memcpy(il->ucode_code.v_addr, pieces.inst, pieces.inst_size);

	D_INFO("uCode instr buf vaddr = 0x%p, paddr = 0x%08x\n",
	       il->ucode_code.v_addr, (u32) il->ucode_code.p_addr);

	/*
	 * Runtime data
	 * NOTE:  Copy into backup buffer will be done in il_up()
	 */
	D_INFO("Copying (but not loading) uCode data len %zd\n",
	       pieces.data_size);
	memcpy(il->ucode_data.v_addr, pieces.data, pieces.data_size);
	memcpy(il->ucode_data_backup.v_addr, pieces.data, pieces.data_size);

	/* Initialization instructions */
	if (pieces.init_size) {
		D_INFO("Copying (but not loading) init instr len %zd\n",
		       pieces.init_size);
		memcpy(il->ucode_init.v_addr, pieces.init, pieces.init_size);
	}

	/* Initialization data */
	if (pieces.init_data_size) {
		D_INFO("Copying (but not loading) init data len %zd\n",
		       pieces.init_data_size);
		memcpy(il->ucode_init_data.v_addr, pieces.init_data,
		       pieces.init_data_size);
	}

	/* Bootstrap instructions */
	D_INFO("Copying (but not loading) boot instr len %zd\n",
	       pieces.boot_size);
	memcpy(il->ucode_boot.v_addr, pieces.boot, pieces.boot_size);

	/*
	 * figure out the offset of chain noise reset and gain commands
	 * base on the size of standard phy calibration commands table size
	 */
	il->_4965.phy_calib_chain_noise_reset_cmd =
	    standard_phy_calibration_size;
	il->_4965.phy_calib_chain_noise_gain_cmd =
	    standard_phy_calibration_size + 1;

	/**************************************************
	 * This is still part of probe() in a sense...
	 *
	 * 9. Setup and register with mac80211 and debugfs
	 **************************************************/
	err = il4965_mac_setup_register(il, max_probe_length);
	if (err)
		goto out_unbind;

	il_dbgfs_register(il, DRV_NAME);

	err = sysfs_create_group(&il->pci_dev->dev.kobj, &il_attribute_group);
	if (err) {
		IL_ERR("failed to create sysfs device attributes\n");
		goto out_unbind;
	}

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);
	complete(&il->_4965.firmware_loading_complete);
	return;

try_again:
	/* try next, if any */
	if (il4965_request_firmware(il, false))
		goto out_unbind;
	release_firmware(ucode_raw);
	return;

err_pci_alloc:
	IL_ERR("failed to allocate pci memory\n");
	il4965_dealloc_ucode_pci(il);
out_unbind:
	complete(&il->_4965.firmware_loading_complete);
	device_release_driver(&il->pci_dev->dev);
	release_firmware(ucode_raw);
}

static const char *const desc_lookup_text[] = {
	"OK",
	"FAIL",
	"BAD_PARAM",
	"BAD_CHECKSUM",
	"NMI_INTERRUPT_WDG",
	"SYSASSERT",
	"FATAL_ERROR",
	"BAD_COMMAND",
	"HW_ERROR_TUNE_LOCK",
	"HW_ERROR_TEMPERATURE",
	"ILLEGAL_CHAN_FREQ",
	"VCC_NOT_STBL",
	"FH49_ERROR",
	"NMI_INTERRUPT_HOST",
	"NMI_INTERRUPT_ACTION_PT",
	"NMI_INTERRUPT_UNKNOWN",
	"UCODE_VERSION_MISMATCH",
	"HW_ERROR_ABS_LOCK",
	"HW_ERROR_CAL_LOCK_FAIL",
	"NMI_INTERRUPT_INST_ACTION_PT",
	"NMI_INTERRUPT_DATA_ACTION_PT",
	"NMI_TRM_HW_ER",
	"NMI_INTERRUPT_TRM",
	"NMI_INTERRUPT_BREAK_POINT",
	"DEBUG_0",
	"DEBUG_1",
	"DEBUG_2",
	"DEBUG_3",
};

static struct {
	char *name;
	u8 num;
} advanced_lookup[] = {
	{
	"NMI_INTERRUPT_WDG", 0x34}, {
	"SYSASSERT", 0x35}, {
	"UCODE_VERSION_MISMATCH", 0x37}, {
	"BAD_COMMAND", 0x38}, {
	"NMI_INTERRUPT_DATA_ACTION_PT", 0x3C}, {
	"FATAL_ERROR", 0x3D}, {
	"NMI_TRM_HW_ERR", 0x46}, {
	"NMI_INTERRUPT_TRM", 0x4C}, {
	"NMI_INTERRUPT_BREAK_POINT", 0x54}, {
	"NMI_INTERRUPT_WDG_RXF_FULL", 0x5C}, {
	"NMI_INTERRUPT_WDG_NO_RBD_RXF_FULL", 0x64}, {
	"NMI_INTERRUPT_HOST", 0x66}, {
	"NMI_INTERRUPT_ACTION_PT", 0x7C}, {
	"NMI_INTERRUPT_UNKNOWN", 0x84}, {
	"NMI_INTERRUPT_INST_ACTION_PT", 0x86}, {
"ADVANCED_SYSASSERT", 0},};

static const char *
il4965_desc_lookup(u32 num)
{
	int i;
	int max = ARRAY_SIZE(desc_lookup_text);

	if (num < max)
		return desc_lookup_text[num];

	max = ARRAY_SIZE(advanced_lookup) - 1;
	for (i = 0; i < max; i++) {
		if (advanced_lookup[i].num == num)
			break;
	}
	return advanced_lookup[i].name;
}

#define ERROR_START_OFFSET  (1 * sizeof(u32))
#define ERROR_ELEM_SIZE     (7 * sizeof(u32))

void
il4965_dump_nic_error_log(struct il_priv *il)
{
	u32 data2, line;
	u32 desc, time, count, base, data1;
	u32 blink1, blink2, ilink1, ilink2;
	u32 pc, hcmd;

	if (il->ucode_type == UCODE_INIT)
		base = le32_to_cpu(il->card_alive_init.error_event_table_ptr);
	else
		base = le32_to_cpu(il->card_alive.error_event_table_ptr);

	if (!il->ops->is_valid_rtc_data_addr(base)) {
		IL_ERR("Not valid error log pointer 0x%08X for %s uCode\n",
		       base, (il->ucode_type == UCODE_INIT) ? "Init" : "RT");
		return;
	}

	count = il_read_targ_mem(il, base);

	if (ERROR_START_OFFSET <= count * ERROR_ELEM_SIZE) {
		IL_ERR("Start IWL Error Log Dump:\n");
		IL_ERR("Status: 0x%08lX, count: %d\n", il->status, count);
	}

	desc = il_read_targ_mem(il, base + 1 * sizeof(u32));
	il->isr_stats.err_code = desc;
	pc = il_read_targ_mem(il, base + 2 * sizeof(u32));
	blink1 = il_read_targ_mem(il, base + 3 * sizeof(u32));
	blink2 = il_read_targ_mem(il, base + 4 * sizeof(u32));
	ilink1 = il_read_targ_mem(il, base + 5 * sizeof(u32));
	ilink2 = il_read_targ_mem(il, base + 6 * sizeof(u32));
	data1 = il_read_targ_mem(il, base + 7 * sizeof(u32));
	data2 = il_read_targ_mem(il, base + 8 * sizeof(u32));
	line = il_read_targ_mem(il, base + 9 * sizeof(u32));
	time = il_read_targ_mem(il, base + 11 * sizeof(u32));
	hcmd = il_read_targ_mem(il, base + 22 * sizeof(u32));

	IL_ERR("Desc                                  Time       "
	       "data1      data2      line\n");
	IL_ERR("%-28s (0x%04X) %010u 0x%08X 0x%08X %u\n",
	       il4965_desc_lookup(desc), desc, time, data1, data2, line);
	IL_ERR("pc      blink1  blink2  ilink1  ilink2  hcmd\n");
	IL_ERR("0x%05X 0x%05X 0x%05X 0x%05X 0x%05X 0x%05X\n", pc, blink1,
	       blink2, ilink1, ilink2, hcmd);
}

static void
il4965_rf_kill_ct_config(struct il_priv *il)
{
	struct il_ct_kill_config cmd;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&il->lock, flags);
	_il_wr(il, CSR_UCODE_DRV_GP1_CLR,
	       CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
	spin_unlock_irqrestore(&il->lock, flags);

	cmd.critical_temperature_R =
	    cpu_to_le32(il->hw_params.ct_kill_threshold);

	ret = il_send_cmd_pdu(il, C_CT_KILL_CONFIG, sizeof(cmd), &cmd);
	if (ret)
		IL_ERR("C_CT_KILL_CONFIG failed\n");
	else
		D_INFO("C_CT_KILL_CONFIG " "succeeded, "
		       "critical temperature is %d\n",
		       il->hw_params.ct_kill_threshold);
}

static const s8 default_queue_to_tx_fifo[] = {
	IL_TX_FIFO_VO,
	IL_TX_FIFO_VI,
	IL_TX_FIFO_BE,
	IL_TX_FIFO_BK,
	IL49_CMD_FIFO_NUM,
	IL_TX_FIFO_UNUSED,
	IL_TX_FIFO_UNUSED,
};

#define IL_MASK(lo, hi) ((1 << (hi)) | ((1 << (hi)) - (1 << (lo))))

static int
il4965_alive_notify(struct il_priv *il)
{
	u32 a;
	unsigned long flags;
	int i, chan;
	u32 reg_val;

	spin_lock_irqsave(&il->lock, flags);

	/* Clear 4965's internal Tx Scheduler data base */
	il->scd_base_addr = il_rd_prph(il, IL49_SCD_SRAM_BASE_ADDR);
	a = il->scd_base_addr + IL49_SCD_CONTEXT_DATA_OFFSET;
	for (; a < il->scd_base_addr + IL49_SCD_TX_STTS_BITMAP_OFFSET; a += 4)
		il_write_targ_mem(il, a, 0);
	for (; a < il->scd_base_addr + IL49_SCD_TRANSLATE_TBL_OFFSET; a += 4)
		il_write_targ_mem(il, a, 0);
	for (;
	     a <
	     il->scd_base_addr +
	     IL49_SCD_TRANSLATE_TBL_OFFSET_QUEUE(il->hw_params.max_txq_num);
	     a += 4)
		il_write_targ_mem(il, a, 0);

	/* Tel 4965 where to find Tx byte count tables */
	il_wr_prph(il, IL49_SCD_DRAM_BASE_ADDR, il->scd_bc_tbls.dma >> 10);

	/* Enable DMA channel */
	for (chan = 0; chan < FH49_TCSR_CHNL_NUM; chan++)
		il_wr(il, FH49_TCSR_CHNL_TX_CONFIG_REG(chan),
		      FH49_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
		      FH49_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);

	/* Update FH chicken bits */
	reg_val = il_rd(il, FH49_TX_CHICKEN_BITS_REG);
	il_wr(il, FH49_TX_CHICKEN_BITS_REG,
	      reg_val | FH49_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN);

	/* Disable chain mode for all queues */
	il_wr_prph(il, IL49_SCD_QUEUECHAIN_SEL, 0);

	/* Initialize each Tx queue (including the command queue) */
	for (i = 0; i < il->hw_params.max_txq_num; i++) {

		/* TFD circular buffer read/write idxes */
		il_wr_prph(il, IL49_SCD_QUEUE_RDPTR(i), 0);
		il_wr(il, HBUS_TARG_WRPTR, 0 | (i << 8));

		/* Max Tx Window size for Scheduler-ACK mode */
		il_write_targ_mem(il,
				  il->scd_base_addr +
				  IL49_SCD_CONTEXT_QUEUE_OFFSET(i),
				  (SCD_WIN_SIZE <<
				   IL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_POS) &
				  IL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_MSK);

		/* Frame limit */
		il_write_targ_mem(il,
				  il->scd_base_addr +
				  IL49_SCD_CONTEXT_QUEUE_OFFSET(i) +
				  sizeof(u32),
				  (SCD_FRAME_LIMIT <<
				   IL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
				  IL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK);

	}
	il_wr_prph(il, IL49_SCD_INTERRUPT_MASK,
		   (1 << il->hw_params.max_txq_num) - 1);

	/* Activate all Tx DMA/FIFO channels */
	il4965_txq_set_sched(il, IL_MASK(0, 6));

	il4965_set_wr_ptrs(il, IL_DEFAULT_CMD_QUEUE_NUM, 0);

	/* make sure all queue are not stopped */
	memset(&il->queue_stopped[0], 0, sizeof(il->queue_stopped));
	for (i = 0; i < 4; i++)
		atomic_set(&il->queue_stop_count[i], 0);

	/* reset to 0 to enable all the queue first */
	il->txq_ctx_active_msk = 0;
	/* Map each Tx/cmd queue to its corresponding fifo */
	BUILD_BUG_ON(ARRAY_SIZE(default_queue_to_tx_fifo) != 7);

	for (i = 0; i < ARRAY_SIZE(default_queue_to_tx_fifo); i++) {
		int ac = default_queue_to_tx_fifo[i];

		il_txq_ctx_activate(il, i);

		if (ac == IL_TX_FIFO_UNUSED)
			continue;

		il4965_tx_queue_set_status(il, &il->txq[i], ac, 0);
	}

	spin_unlock_irqrestore(&il->lock, flags);

	return 0;
}

/*
 * il4965_alive_start - called after N_ALIVE notification received
 *                   from protocol/runtime uCode (initialization uCode's
 *                   Alive gets handled by il_init_alive_start()).
 */
static void
il4965_alive_start(struct il_priv *il)
{
	int ret = 0;

	D_INFO("Runtime Alive received.\n");

	if (il->card_alive.is_valid != UCODE_VALID_OK) {
		/* We had an error bringing up the hardware, so take it
		 * all the way back down so we can try again */
		D_INFO("Alive failed.\n");
		goto restart;
	}

	/* Initialize uCode has loaded Runtime uCode ... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "runtime" alive if code weren't properly loaded.  */
	if (il4965_verify_ucode(il)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		D_INFO("Bad runtime uCode load.\n");
		goto restart;
	}

	ret = il4965_alive_notify(il);
	if (ret) {
		IL_WARN("Could not complete ALIVE transition [ntf]: %d\n", ret);
		goto restart;
	}

	/* After the ALIVE response, we can send host commands to the uCode */
	set_bit(S_ALIVE, &il->status);

	/* Enable watchdog to monitor the driver tx queues */
	il_setup_watchdog(il);

	if (il_is_rfkill(il))
		return;

	ieee80211_wake_queues(il->hw);

	il->active_rate = RATES_MASK;

	il_power_update_mode(il, true);
	D_INFO("Updated power mode\n");

	if (il_is_associated(il)) {
		struct il_rxon_cmd *active_rxon =
		    (struct il_rxon_cmd *)&il->active;
		/* apply any changes in staging */
		il->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	} else {
		/* Initialize our rx_config data */
		il_connection_init_rx_config(il);

		if (il->ops->set_rxon_chain)
			il->ops->set_rxon_chain(il);
	}

	/* Configure bluetooth coexistence if enabled */
	il_send_bt_config(il);

	il4965_reset_run_time_calib(il);

	set_bit(S_READY, &il->status);

	/* Configure the adapter for unassociated operation */
	il_commit_rxon(il);

	/* At this point, the NIC is initialized and operational */
	il4965_rf_kill_ct_config(il);

	D_INFO("ALIVE processing complete.\n");
	wake_up(&il->wait_command_queue);

	return;

restart:
	queue_work(il->workqueue, &il->restart);
}

static void il4965_cancel_deferred_work(struct il_priv *il);

static void
__il4965_down(struct il_priv *il)
{
	unsigned long flags;
	int exit_pending;

	D_INFO(DRV_NAME " is going down\n");

	il_scan_cancel_timeout(il, 200);

	exit_pending = test_and_set_bit(S_EXIT_PENDING, &il->status);

	/* Stop TX queues watchdog. We need to have S_EXIT_PENDING bit set
	 * to prevent rearm timer */
	del_timer_sync(&il->watchdog);

	il_clear_ucode_stations(il);

	/* FIXME: race conditions ? */
	spin_lock_irq(&il->sta_lock);
	/*
	 * Remove all key information that is not stored as part
	 * of station information since mac80211 may not have had
	 * a chance to remove all the keys. When device is
	 * reconfigured by mac80211 after an error all keys will
	 * be reconfigured.
	 */
	memset(il->_4965.wep_keys, 0, sizeof(il->_4965.wep_keys));
	il->_4965.key_mapping_keys = 0;
	spin_unlock_irq(&il->sta_lock);

	il_dealloc_bcast_stations(il);
	il_clear_driver_stations(il);

	/* Unblock any waiting calls */
	wake_up_all(&il->wait_command_queue);

	/* Wipe out the EXIT_PENDING status bit if we are not actually
	 * exiting the module */
	if (!exit_pending)
		clear_bit(S_EXIT_PENDING, &il->status);

	/* stop and reset the on-board processor */
	_il_wr(il, CSR_RESET, CSR_RESET_REG_FLAG_NEVO_RESET);

	/* tell the device to stop sending interrupts */
	spin_lock_irqsave(&il->lock, flags);
	il_disable_interrupts(il);
	spin_unlock_irqrestore(&il->lock, flags);
	il4965_synchronize_irq(il);

	if (il->mac80211_registered)
		ieee80211_stop_queues(il->hw);

	/* If we have not previously called il_init() then
	 * clear all bits but the RF Kill bit and return */
	if (!il_is_init(il)) {
		il->status =
		    test_bit(S_RFKILL, &il->status) << S_RFKILL |
		    test_bit(S_GEO_CONFIGURED, &il->status) << S_GEO_CONFIGURED |
		    test_bit(S_EXIT_PENDING, &il->status) << S_EXIT_PENDING;
		goto exit;
	}

	/* ...otherwise clear out all the status bits but the RF Kill
	 * bit and continue taking the NIC down. */
	il->status &=
	    test_bit(S_RFKILL, &il->status) << S_RFKILL |
	    test_bit(S_GEO_CONFIGURED, &il->status) << S_GEO_CONFIGURED |
	    test_bit(S_FW_ERROR, &il->status) << S_FW_ERROR |
	    test_bit(S_EXIT_PENDING, &il->status) << S_EXIT_PENDING;

	/*
	 * We disabled and synchronized interrupt, and priv->mutex is taken, so
	 * here is the only thread which will program device registers, but
	 * still have lockdep assertions, so we are taking reg_lock.
	 */
	spin_lock_irq(&il->reg_lock);
	/* FIXME: il_grab_nic_access if rfkill is off ? */

	il4965_txq_ctx_stop(il);
	il4965_rxq_stop(il);
	/* Power-down device's busmaster DMA clocks */
	_il_wr_prph(il, APMG_CLK_DIS_REG, APMG_CLK_VAL_DMA_CLK_RQT);
	udelay(5);
	/* Make sure (redundant) we've released our request to stay awake */
	_il_clear_bit(il, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	/* Stop the device, and put it in low power state */
	_il_apm_stop(il);

	spin_unlock_irq(&il->reg_lock);

	il4965_txq_ctx_unmap(il);
exit:
	memset(&il->card_alive, 0, sizeof(struct il_alive_resp));

	dev_kfree_skb(il->beacon_skb);
	il->beacon_skb = NULL;

	/* clear out any free frames */
	il4965_clear_free_frames(il);
}

static void
il4965_down(struct il_priv *il)
{
	mutex_lock(&il->mutex);
	__il4965_down(il);
	mutex_unlock(&il->mutex);

	il4965_cancel_deferred_work(il);
}


static void
il4965_set_hw_ready(struct il_priv *il)
{
	int ret;

	il_set_bit(il, CSR_HW_IF_CONFIG_REG,
		   CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	/* See if we got it */
	ret = _il_poll_bit(il, CSR_HW_IF_CONFIG_REG,
			   CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
			   CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
			   100);
	if (ret >= 0)
		il->hw_ready = true;

	D_INFO("hardware %s ready\n", (il->hw_ready) ? "" : "not");
}

static void
il4965_prepare_card_hw(struct il_priv *il)
{
	int ret;

	il->hw_ready = false;

	il4965_set_hw_ready(il);
	if (il->hw_ready)
		return;

	/* If HW is not ready, prepare the conditions to check again */
	il_set_bit(il, CSR_HW_IF_CONFIG_REG, CSR_HW_IF_CONFIG_REG_PREPARE);

	ret =
	    _il_poll_bit(il, CSR_HW_IF_CONFIG_REG,
			 ~CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE,
			 CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE, 150000);

	/* HW should be ready by now, check again. */
	if (ret != -ETIMEDOUT)
		il4965_set_hw_ready(il);
}

#define MAX_HW_RESTARTS 5

static int
__il4965_up(struct il_priv *il)
{
	int i;
	int ret;

	if (test_bit(S_EXIT_PENDING, &il->status)) {
		IL_WARN("Exit pending; will not bring the NIC up\n");
		return -EIO;
	}

	if (!il->ucode_data_backup.v_addr || !il->ucode_data.v_addr) {
		IL_ERR("ucode not available for device bringup\n");
		return -EIO;
	}

	ret = il4965_alloc_bcast_station(il);
	if (ret) {
		il_dealloc_bcast_stations(il);
		return ret;
	}

	il4965_prepare_card_hw(il);
	if (!il->hw_ready) {
		il_dealloc_bcast_stations(il);
		IL_ERR("HW not ready\n");
		return -EIO;
	}

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (_il_rd(il, CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(S_RFKILL, &il->status);
	else {
		set_bit(S_RFKILL, &il->status);
		wiphy_rfkill_set_hw_state(il->hw->wiphy, true);

		il_dealloc_bcast_stations(il);
		il_enable_rfkill_int(il);
		IL_WARN("Radio disabled by HW RF Kill switch\n");
		return 0;
	}

	_il_wr(il, CSR_INT, 0xFFFFFFFF);

	/* must be initialised before il_hw_nic_init */
	il->cmd_queue = IL_DEFAULT_CMD_QUEUE_NUM;

	ret = il4965_hw_nic_init(il);
	if (ret) {
		IL_ERR("Unable to init nic\n");
		il_dealloc_bcast_stations(il);
		return ret;
	}

	/* make sure rfkill handshake bits are cleared */
	_il_wr(il, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	_il_wr(il, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable host interrupts */
	_il_wr(il, CSR_INT, 0xFFFFFFFF);
	il_enable_interrupts(il);

	/* really make sure rfkill handshake bits are cleared */
	_il_wr(il, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	_il_wr(il, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);

	/* Copy original ucode data image from disk into backup cache.
	 * This will be used to initialize the on-board processor's
	 * data SRAM for a clean start when the runtime program first loads. */
	memcpy(il->ucode_data_backup.v_addr, il->ucode_data.v_addr,
	       il->ucode_data.len);

	for (i = 0; i < MAX_HW_RESTARTS; i++) {

		/* load bootstrap state machine,
		 * load bootstrap program into processor's memory,
		 * prepare to load the "initialize" uCode */
		ret = il->ops->load_ucode(il);

		if (ret) {
			IL_ERR("Unable to set up bootstrap uCode: %d\n", ret);
			continue;
		}

		/* start card; "initialize" will load runtime ucode */
		il4965_nic_start(il);

		D_INFO(DRV_NAME " is coming up\n");

		return 0;
	}

	set_bit(S_EXIT_PENDING, &il->status);
	__il4965_down(il);
	clear_bit(S_EXIT_PENDING, &il->status);

	/* tried to restart and config the device for as long as our
	 * patience could withstand */
	IL_ERR("Unable to initialize device after %d attempts.\n", i);
	return -EIO;
}

/*****************************************************************************
 *
 * Workqueue callbacks
 *
 *****************************************************************************/

static void
il4965_bg_init_alive_start(struct work_struct *data)
{
	struct il_priv *il =
	    container_of(data, struct il_priv, init_alive_start.work);

	mutex_lock(&il->mutex);
	if (test_bit(S_EXIT_PENDING, &il->status))
		goto out;

	il->ops->init_alive_start(il);
out:
	mutex_unlock(&il->mutex);
}

static void
il4965_bg_alive_start(struct work_struct *data)
{
	struct il_priv *il =
	    container_of(data, struct il_priv, alive_start.work);

	mutex_lock(&il->mutex);
	if (test_bit(S_EXIT_PENDING, &il->status))
		goto out;

	il4965_alive_start(il);
out:
	mutex_unlock(&il->mutex);
}

static void
il4965_bg_run_time_calib_work(struct work_struct *work)
{
	struct il_priv *il = container_of(work, struct il_priv,
					  run_time_calib_work);

	mutex_lock(&il->mutex);

	if (test_bit(S_EXIT_PENDING, &il->status) ||
	    test_bit(S_SCANNING, &il->status)) {
		mutex_unlock(&il->mutex);
		return;
	}

	if (il->start_calib) {
		il4965_chain_noise_calibration(il, (void *)&il->_4965.stats);
		il4965_sensitivity_calibration(il, (void *)&il->_4965.stats);
	}

	mutex_unlock(&il->mutex);
}

static void
il4965_bg_restart(struct work_struct *data)
{
	struct il_priv *il = container_of(data, struct il_priv, restart);

	if (test_bit(S_EXIT_PENDING, &il->status))
		return;

	if (test_and_clear_bit(S_FW_ERROR, &il->status)) {
		mutex_lock(&il->mutex);
		il->is_open = 0;

		__il4965_down(il);

		mutex_unlock(&il->mutex);
		il4965_cancel_deferred_work(il);
		ieee80211_restart_hw(il->hw);
	} else {
		il4965_down(il);

		mutex_lock(&il->mutex);
		if (test_bit(S_EXIT_PENDING, &il->status)) {
			mutex_unlock(&il->mutex);
			return;
		}

		__il4965_up(il);
		mutex_unlock(&il->mutex);
	}
}

static void
il4965_bg_rx_replenish(struct work_struct *data)
{
	struct il_priv *il = container_of(data, struct il_priv, rx_replenish);

	if (test_bit(S_EXIT_PENDING, &il->status))
		return;

	mutex_lock(&il->mutex);
	il4965_rx_replenish(il);
	mutex_unlock(&il->mutex);
}

/*****************************************************************************
 *
 * mac80211 entry point functions
 *
 *****************************************************************************/

#define UCODE_READY_TIMEOUT	(4 * HZ)

/*
 * Not a mac80211 entry point function, but it fits in with all the
 * other mac80211 functions grouped here.
 */
static int
il4965_mac_setup_register(struct il_priv *il, u32 max_probe_length)
{
	int ret;
	struct ieee80211_hw *hw = il->hw;

	hw->rate_control_algorithm = "iwl-4965-rs";

	/* Tell mac80211 our characteristics */
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, SPECTRUM_MGMT);
	ieee80211_hw_set(hw, NEED_DTIM_BEFORE_ASSOC);
	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	if (il->cfg->sku & IL_SKU_N)
		hw->wiphy->features |= NL80211_FEATURE_DYNAMIC_SMPS |
				       NL80211_FEATURE_STATIC_SMPS;

	hw->sta_data_size = sizeof(struct il_station_priv);
	hw->vif_data_size = sizeof(struct il_vif_priv);

	hw->wiphy->interface_modes =
	    BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
	hw->wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG |
				       REGULATORY_DISABLE_BEACON_HINTS;

	/*
	 * For now, disable PS by default because it affects
	 * RX performance significantly.
	 */
	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->max_scan_ssids = PROBE_OPTION_MAX;
	/* we create the 802.11 header and a zero-length SSID element */
	hw->wiphy->max_scan_ie_len = max_probe_length - 24 - 2;

	/* Default value; 4 EDCA QOS priorities */
	hw->queues = 4;

	hw->max_listen_interval = IL_CONN_MAX_LISTEN_INTERVAL;

	if (il->bands[NL80211_BAND_2GHZ].n_channels)
		il->hw->wiphy->bands[NL80211_BAND_2GHZ] =
		    &il->bands[NL80211_BAND_2GHZ];
	if (il->bands[NL80211_BAND_5GHZ].n_channels)
		il->hw->wiphy->bands[NL80211_BAND_5GHZ] =
		    &il->bands[NL80211_BAND_5GHZ];

	il_leds_init(il);

	wiphy_ext_feature_set(il->hw->wiphy, NL80211_EXT_FEATURE_CQM_RSSI_LIST);

	ret = ieee80211_register_hw(il->hw);
	if (ret) {
		IL_ERR("Failed to register hw (error %d)\n", ret);
		return ret;
	}
	il->mac80211_registered = 1;

	return 0;
}

int
il4965_mac_start(struct ieee80211_hw *hw)
{
	struct il_priv *il = hw->priv;
	int ret;

	D_MAC80211("enter\n");

	/* we should be verifying the device is ready to be opened */
	mutex_lock(&il->mutex);
	ret = __il4965_up(il);
	mutex_unlock(&il->mutex);

	if (ret)
		return ret;

	if (il_is_rfkill(il))
		goto out;

	D_INFO("Start UP work done.\n");

	/* Wait for START_ALIVE from Run Time ucode. Otherwise callbacks from
	 * mac80211 will not be run successfully. */
	ret = wait_event_timeout(il->wait_command_queue,
				 test_bit(S_READY, &il->status),
				 UCODE_READY_TIMEOUT);
	if (!ret) {
		if (!test_bit(S_READY, &il->status)) {
			IL_ERR("START_ALIVE timeout after %dms.\n",
				jiffies_to_msecs(UCODE_READY_TIMEOUT));
			return -ETIMEDOUT;
		}
	}

	il4965_led_enable(il);

out:
	il->is_open = 1;
	D_MAC80211("leave\n");
	return 0;
}

void
il4965_mac_stop(struct ieee80211_hw *hw)
{
	struct il_priv *il = hw->priv;

	D_MAC80211("enter\n");

	if (!il->is_open)
		return;

	il->is_open = 0;

	il4965_down(il);

	flush_workqueue(il->workqueue);

	/* User space software may expect getting rfkill changes
	 * even if interface is down */
	_il_wr(il, CSR_INT, 0xFFFFFFFF);
	il_enable_rfkill_int(il);

	D_MAC80211("leave\n");
}

void
il4965_mac_tx(struct ieee80211_hw *hw,
	      struct ieee80211_tx_control *control,
	      struct sk_buff *skb)
{
	struct il_priv *il = hw->priv;

	D_MACDUMP("enter\n");

	D_TX("dev->xmit(%d bytes) at rate 0x%02x\n", skb->len,
	     ieee80211_get_tx_rate(hw, IEEE80211_SKB_CB(skb))->bitrate);

	if (il4965_tx_skb(il, control->sta, skb))
		dev_kfree_skb_any(skb);

	D_MACDUMP("leave\n");
}

void
il4965_mac_update_tkip_key(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_key_conf *keyconf,
			   struct ieee80211_sta *sta, u32 iv32, u16 * phase1key)
{
	struct il_priv *il = hw->priv;

	D_MAC80211("enter\n");

	il4965_update_tkip_key(il, keyconf, sta, iv32, phase1key);

	D_MAC80211("leave\n");
}

int
il4965_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		   struct ieee80211_key_conf *key)
{
	struct il_priv *il = hw->priv;
	int ret;
	u8 sta_id;
	bool is_default_wep_key = false;

	D_MAC80211("enter\n");

	if (il->cfg->mod_params->sw_crypto) {
		D_MAC80211("leave - hwcrypto disabled\n");
		return -EOPNOTSUPP;
	}

	/*
	 * To support IBSS RSN, don't program group keys in IBSS, the
	 * hardware will then not attempt to decrypt the frames.
	 */
	if (vif->type == NL80211_IFTYPE_ADHOC &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
		D_MAC80211("leave - ad-hoc group key\n");
		return -EOPNOTSUPP;
	}

	sta_id = il_sta_id_or_broadcast(il, sta);
	if (sta_id == IL_INVALID_STATION)
		return -EINVAL;

	mutex_lock(&il->mutex);
	il_scan_cancel_timeout(il, 100);

	/*
	 * If we are getting WEP group key and we didn't receive any key mapping
	 * so far, we are in legacy wep mode (group key only), otherwise we are
	 * in 1X mode.
	 * In legacy wep mode, we use another host command to the uCode.
	 */
	if ((key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	     key->cipher == WLAN_CIPHER_SUITE_WEP104) && !sta) {
		if (cmd == SET_KEY)
			is_default_wep_key = !il->_4965.key_mapping_keys;
		else
			is_default_wep_key =
			    (key->hw_key_idx == HW_KEY_DEFAULT);
	}

	switch (cmd) {
	case SET_KEY:
		if (is_default_wep_key)
			ret = il4965_set_default_wep_key(il, key);
		else
			ret = il4965_set_dynamic_key(il, key, sta_id);

		D_MAC80211("enable hwcrypto key\n");
		break;
	case DISABLE_KEY:
		if (is_default_wep_key)
			ret = il4965_remove_default_wep_key(il, key);
		else
			ret = il4965_remove_dynamic_key(il, key, sta_id);

		D_MAC80211("disable hwcrypto key\n");
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&il->mutex);
	D_MAC80211("leave\n");

	return ret;
}

int
il4965_mac_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_ampdu_params *params)
{
	struct il_priv *il = hw->priv;
	int ret = -EINVAL;
	struct ieee80211_sta *sta = params->sta;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;
	u16 *ssn = &params->ssn;

	D_HT("A-MPDU action on addr %pM tid %d\n", sta->addr, tid);

	if (!(il->cfg->sku & IL_SKU_N))
		return -EACCES;

	mutex_lock(&il->mutex);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		D_HT("start Rx\n");
		ret = il4965_sta_rx_agg_start(il, sta, tid, *ssn);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		D_HT("stop Rx\n");
		ret = il4965_sta_rx_agg_stop(il, sta, tid);
		if (test_bit(S_EXIT_PENDING, &il->status))
			ret = 0;
		break;
	case IEEE80211_AMPDU_TX_START:
		D_HT("start Tx\n");
		ret = il4965_tx_agg_start(il, vif, sta, tid, ssn);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		D_HT("stop Tx\n");
		ret = il4965_tx_agg_stop(il, vif, sta, tid);
		if (test_bit(S_EXIT_PENDING, &il->status))
			ret = 0;
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ret = 0;
		break;
	}
	mutex_unlock(&il->mutex);

	return ret;
}

int
il4965_mac_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct il_priv *il = hw->priv;
	struct il_station_priv *sta_priv = (void *)sta->drv_priv;
	bool is_ap = vif->type == NL80211_IFTYPE_STATION;
	int ret;
	u8 sta_id;

	D_INFO("received request to add station %pM\n", sta->addr);
	mutex_lock(&il->mutex);
	D_INFO("proceeding to add station %pM\n", sta->addr);
	sta_priv->common.sta_id = IL_INVALID_STATION;

	atomic_set(&sta_priv->pending_frames, 0);

	ret =
	    il_add_station_common(il, sta->addr, is_ap, sta, &sta_id);
	if (ret) {
		IL_ERR("Unable to add station %pM (%d)\n", sta->addr, ret);
		/* Should we return success if return code is EEXIST ? */
		mutex_unlock(&il->mutex);
		return ret;
	}

	sta_priv->common.sta_id = sta_id;

	/* Initialize rate scaling */
	D_INFO("Initializing rate scaling for station %pM\n", sta->addr);
	il4965_rs_rate_init(il, sta, sta_id);
	mutex_unlock(&il->mutex);

	return 0;
}

void
il4965_mac_channel_switch(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_channel_switch *ch_switch)
{
	struct il_priv *il = hw->priv;
	const struct il_channel_info *ch_info;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_channel *channel = ch_switch->chandef.chan;
	struct il_ht_config *ht_conf = &il->current_ht_config;
	u16 ch;

	D_MAC80211("enter\n");

	mutex_lock(&il->mutex);

	if (il_is_rfkill(il))
		goto out;

	if (test_bit(S_EXIT_PENDING, &il->status) ||
	    test_bit(S_SCANNING, &il->status) ||
	    test_bit(S_CHANNEL_SWITCH_PENDING, &il->status))
		goto out;

	if (!il_is_associated(il))
		goto out;

	if (!il->ops->set_channel_switch)
		goto out;

	ch = channel->hw_value;
	if (le16_to_cpu(il->active.channel) == ch)
		goto out;

	ch_info = il_get_channel_info(il, channel->band, ch);
	if (!il_is_channel_valid(ch_info)) {
		D_MAC80211("invalid channel\n");
		goto out;
	}

	spin_lock_irq(&il->lock);

	il->current_ht_config.smps = conf->smps_mode;

	/* Configure HT40 channels */
	switch (cfg80211_get_chandef_type(&ch_switch->chandef)) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		il->ht.is_40mhz = false;
		il->ht.extension_chan_offset = IEEE80211_HT_PARAM_CHA_SEC_NONE;
		break;
	case NL80211_CHAN_HT40MINUS:
		il->ht.extension_chan_offset = IEEE80211_HT_PARAM_CHA_SEC_BELOW;
		il->ht.is_40mhz = true;
		break;
	case NL80211_CHAN_HT40PLUS:
		il->ht.extension_chan_offset = IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
		il->ht.is_40mhz = true;
		break;
	}

	if ((le16_to_cpu(il->staging.channel) != ch))
		il->staging.flags = 0;

	il_set_rxon_channel(il, channel);
	il_set_rxon_ht(il, ht_conf);
	il_set_flags_for_band(il, channel->band, il->vif);

	spin_unlock_irq(&il->lock);

	il_set_rate(il);
	/*
	 * at this point, staging_rxon has the
	 * configuration for channel switch
	 */
	set_bit(S_CHANNEL_SWITCH_PENDING, &il->status);
	il->switch_channel = cpu_to_le16(ch);
	if (il->ops->set_channel_switch(il, ch_switch)) {
		clear_bit(S_CHANNEL_SWITCH_PENDING, &il->status);
		il->switch_channel = 0;
		ieee80211_chswitch_done(il->vif, false);
	}

out:
	mutex_unlock(&il->mutex);
	D_MAC80211("leave\n");
}

void
il4965_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags,
			unsigned int *total_flags, u64 multicast)
{
	struct il_priv *il = hw->priv;
	__le32 filter_or = 0, filter_nand = 0;

#define CHK(test, flag)	do { \
	if (*total_flags & (test))		\
		filter_or |= (flag);		\
	else					\
		filter_nand |= (flag);		\
	} while (0)

	D_MAC80211("Enter: changed: 0x%x, total: 0x%x\n", changed_flags,
		   *total_flags);

	CHK(FIF_OTHER_BSS, RXON_FILTER_PROMISC_MSK);
	/* Setting _just_ RXON_FILTER_CTL2HOST_MSK causes FH errors */
	CHK(FIF_CONTROL, RXON_FILTER_CTL2HOST_MSK | RXON_FILTER_PROMISC_MSK);
	CHK(FIF_BCN_PRBRESP_PROMISC, RXON_FILTER_BCON_AWARE_MSK);

#undef CHK

	mutex_lock(&il->mutex);

	il->staging.filter_flags &= ~filter_nand;
	il->staging.filter_flags |= filter_or;

	/*
	 * Not committing directly because hardware can perform a scan,
	 * but we'll eventually commit the filter flags change anyway.
	 */

	mutex_unlock(&il->mutex);

	/*
	 * Receiving all multicast frames is always enabled by the
	 * default flags setup in il_connection_init_rx_config()
	 * since we currently do not support programming multicast
	 * filters into the device.
	 */
	*total_flags &=
	    FIF_OTHER_BSS | FIF_ALLMULTI |
	    FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL;
}

/*****************************************************************************
 *
 * driver setup and teardown
 *
 *****************************************************************************/

static void
il4965_bg_txpower_work(struct work_struct *work)
{
	struct il_priv *il = container_of(work, struct il_priv,
					  txpower_work);

	mutex_lock(&il->mutex);

	/* If a scan happened to start before we got here
	 * then just return; the stats notification will
	 * kick off another scheduled work to compensate for
	 * any temperature delta we missed here. */
	if (test_bit(S_EXIT_PENDING, &il->status) ||
	    test_bit(S_SCANNING, &il->status))
		goto out;

	/* Regardless of if we are associated, we must reconfigure the
	 * TX power since frames can be sent on non-radar channels while
	 * not associated */
	il->ops->send_tx_power(il);

	/* Update last_temperature to keep is_calib_needed from running
	 * when it isn't needed... */
	il->last_temperature = il->temperature;
out:
	mutex_unlock(&il->mutex);
}

static void
il4965_setup_deferred_work(struct il_priv *il)
{
	il->workqueue = create_singlethread_workqueue(DRV_NAME);

	init_waitqueue_head(&il->wait_command_queue);

	INIT_WORK(&il->restart, il4965_bg_restart);
	INIT_WORK(&il->rx_replenish, il4965_bg_rx_replenish);
	INIT_WORK(&il->run_time_calib_work, il4965_bg_run_time_calib_work);
	INIT_DELAYED_WORK(&il->init_alive_start, il4965_bg_init_alive_start);
	INIT_DELAYED_WORK(&il->alive_start, il4965_bg_alive_start);

	il_setup_scan_deferred_work(il);

	INIT_WORK(&il->txpower_work, il4965_bg_txpower_work);

	timer_setup(&il->stats_periodic, il4965_bg_stats_periodic, 0);

	timer_setup(&il->watchdog, il_bg_watchdog, 0);

	tasklet_setup(&il->irq_tasklet, il4965_irq_tasklet);
}

static void
il4965_cancel_deferred_work(struct il_priv *il)
{
	cancel_work_sync(&il->txpower_work);
	cancel_delayed_work_sync(&il->init_alive_start);
	cancel_delayed_work(&il->alive_start);
	cancel_work_sync(&il->run_time_calib_work);

	il_cancel_scan_deferred_work(il);

	del_timer_sync(&il->stats_periodic);
}

static void
il4965_init_hw_rates(struct il_priv *il, struct ieee80211_rate *rates)
{
	int i;

	for (i = 0; i < RATE_COUNT_LEGACY; i++) {
		rates[i].bitrate = il_rates[i].ieee * 5;
		rates[i].hw_value = i;	/* Rate scaling will work on idxes */
		rates[i].hw_value_short = i;
		rates[i].flags = 0;
		if ((i >= IL_FIRST_CCK_RATE) && (i <= IL_LAST_CCK_RATE)) {
			/*
			 * If CCK != 1M then set short preamble rate flag.
			 */
			rates[i].flags |=
			    (il_rates[i].plcp ==
			     RATE_1M_PLCP) ? 0 : IEEE80211_RATE_SHORT_PREAMBLE;
		}
	}
}

/*
 * Acquire il->lock before calling this function !
 */
void
il4965_set_wr_ptrs(struct il_priv *il, int txq_id, u32 idx)
{
	il_wr(il, HBUS_TARG_WRPTR, (idx & 0xff) | (txq_id << 8));
	il_wr_prph(il, IL49_SCD_QUEUE_RDPTR(txq_id), idx);
}

void
il4965_tx_queue_set_status(struct il_priv *il, struct il_tx_queue *txq,
			   int tx_fifo_id, int scd_retry)
{
	int txq_id = txq->q.id;

	/* Find out whether to activate Tx queue */
	int active = test_bit(txq_id, &il->txq_ctx_active_msk) ? 1 : 0;

	/* Set up and activate */
	il_wr_prph(il, IL49_SCD_QUEUE_STATUS_BITS(txq_id),
		   (active << IL49_SCD_QUEUE_STTS_REG_POS_ACTIVE) |
		   (tx_fifo_id << IL49_SCD_QUEUE_STTS_REG_POS_TXF) |
		   (scd_retry << IL49_SCD_QUEUE_STTS_REG_POS_WSL) |
		   (scd_retry << IL49_SCD_QUEUE_STTS_REG_POS_SCD_ACK) |
		   IL49_SCD_QUEUE_STTS_REG_MSK);

	txq->sched_retry = scd_retry;

	D_INFO("%s %s Queue %d on AC %d\n", active ? "Activate" : "Deactivate",
	       scd_retry ? "BA" : "AC", txq_id, tx_fifo_id);
}

static const struct ieee80211_ops il4965_mac_ops = {
	.tx = il4965_mac_tx,
	.start = il4965_mac_start,
	.stop = il4965_mac_stop,
	.add_interface = il_mac_add_interface,
	.remove_interface = il_mac_remove_interface,
	.change_interface = il_mac_change_interface,
	.config = il_mac_config,
	.configure_filter = il4965_configure_filter,
	.set_key = il4965_mac_set_key,
	.update_tkip_key = il4965_mac_update_tkip_key,
	.conf_tx = il_mac_conf_tx,
	.reset_tsf = il_mac_reset_tsf,
	.bss_info_changed = il_mac_bss_info_changed,
	.ampdu_action = il4965_mac_ampdu_action,
	.hw_scan = il_mac_hw_scan,
	.sta_add = il4965_mac_sta_add,
	.sta_remove = il_mac_sta_remove,
	.channel_switch = il4965_mac_channel_switch,
	.tx_last_beacon = il_mac_tx_last_beacon,
	.flush = il_mac_flush,
};

static int
il4965_init_drv(struct il_priv *il)
{
	int ret;

	spin_lock_init(&il->sta_lock);
	spin_lock_init(&il->hcmd_lock);

	INIT_LIST_HEAD(&il->free_frames);

	mutex_init(&il->mutex);

	il->ieee_channels = NULL;
	il->ieee_rates = NULL;
	il->band = NL80211_BAND_2GHZ;

	il->iw_mode = NL80211_IFTYPE_STATION;
	il->current_ht_config.smps = IEEE80211_SMPS_STATIC;
	il->missed_beacon_threshold = IL_MISSED_BEACON_THRESHOLD_DEF;

	/* initialize force reset */
	il->force_reset.reset_duration = IL_DELAY_NEXT_FORCE_FW_RELOAD;

	/* Choose which receivers/antennas to use */
	if (il->ops->set_rxon_chain)
		il->ops->set_rxon_chain(il);

	il_init_scan_params(il);

	ret = il_init_channel_map(il);
	if (ret) {
		IL_ERR("initializing regulatory failed: %d\n", ret);
		goto err;
	}

	ret = il_init_geos(il);
	if (ret) {
		IL_ERR("initializing geos failed: %d\n", ret);
		goto err_free_channel_map;
	}
	il4965_init_hw_rates(il, il->ieee_rates);

	return 0;

err_free_channel_map:
	il_free_channel_map(il);
err:
	return ret;
}

static void
il4965_uninit_drv(struct il_priv *il)
{
	il_free_geos(il);
	il_free_channel_map(il);
	kfree(il->scan_cmd);
}

static void
il4965_hw_detect(struct il_priv *il)
{
	il->hw_rev = _il_rd(il, CSR_HW_REV);
	il->hw_wa_rev = _il_rd(il, CSR_HW_REV_WA_REG);
	il->rev_id = il->pci_dev->revision;
	D_INFO("HW Revision ID = 0x%X\n", il->rev_id);
}

static const struct il_sensitivity_ranges il4965_sensitivity = {
	.min_nrg_cck = 97,
	.max_nrg_cck = 0,	/* not used, set to 0 */

	.auto_corr_min_ofdm = 85,
	.auto_corr_min_ofdm_mrc = 170,
	.auto_corr_min_ofdm_x1 = 105,
	.auto_corr_min_ofdm_mrc_x1 = 220,

	.auto_corr_max_ofdm = 120,
	.auto_corr_max_ofdm_mrc = 210,
	.auto_corr_max_ofdm_x1 = 140,
	.auto_corr_max_ofdm_mrc_x1 = 270,

	.auto_corr_min_cck = 125,
	.auto_corr_max_cck = 200,
	.auto_corr_min_cck_mrc = 200,
	.auto_corr_max_cck_mrc = 400,

	.nrg_th_cck = 100,
	.nrg_th_ofdm = 100,

	.barker_corr_th_min = 190,
	.barker_corr_th_min_mrc = 390,
	.nrg_th_cca = 62,
};

static void
il4965_set_hw_params(struct il_priv *il)
{
	il->hw_params.bcast_id = IL4965_BROADCAST_ID;
	il->hw_params.max_rxq_size = RX_QUEUE_SIZE;
	il->hw_params.max_rxq_log = RX_QUEUE_SIZE_LOG;
	if (il->cfg->mod_params->amsdu_size_8K)
		il->hw_params.rx_page_order = get_order(IL_RX_BUF_SIZE_8K);
	else
		il->hw_params.rx_page_order = get_order(IL_RX_BUF_SIZE_4K);

	il->hw_params.max_beacon_itrvl = IL_MAX_UCODE_BEACON_INTERVAL;

	if (il->cfg->mod_params->disable_11n)
		il->cfg->sku &= ~IL_SKU_N;

	if (il->cfg->mod_params->num_of_queues >= IL_MIN_NUM_QUEUES &&
	    il->cfg->mod_params->num_of_queues <= IL49_NUM_QUEUES)
		il->cfg->num_of_queues =
		    il->cfg->mod_params->num_of_queues;

	il->hw_params.max_txq_num = il->cfg->num_of_queues;
	il->hw_params.dma_chnl_num = FH49_TCSR_CHNL_NUM;
	il->hw_params.scd_bc_tbls_size =
	    il->cfg->num_of_queues *
	    sizeof(struct il4965_scd_bc_tbl);

	il->hw_params.tfd_size = sizeof(struct il_tfd);
	il->hw_params.max_stations = IL4965_STATION_COUNT;
	il->hw_params.max_data_size = IL49_RTC_DATA_SIZE;
	il->hw_params.max_inst_size = IL49_RTC_INST_SIZE;
	il->hw_params.max_bsm_size = BSM_SRAM_SIZE;
	il->hw_params.ht40_channel = BIT(NL80211_BAND_5GHZ);

	il->hw_params.rx_wrt_ptr_reg = FH49_RSCSR_CHNL0_WPTR;

	il->hw_params.tx_chains_num = il4965_num_of_ant(il->cfg->valid_tx_ant);
	il->hw_params.rx_chains_num = il4965_num_of_ant(il->cfg->valid_rx_ant);
	il->hw_params.valid_tx_ant = il->cfg->valid_tx_ant;
	il->hw_params.valid_rx_ant = il->cfg->valid_rx_ant;

	il->hw_params.ct_kill_threshold =
	   celsius_to_kelvin(CT_KILL_THRESHOLD_LEGACY);

	il->hw_params.sens = &il4965_sensitivity;
	il->hw_params.beacon_time_tsf_bits = IL4965_EXT_BEACON_TIME_POS;
}

static int
il4965_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = 0;
	struct il_priv *il;
	struct ieee80211_hw *hw;
	struct il_cfg *cfg = (struct il_cfg *)(ent->driver_data);
	unsigned long flags;
	u16 pci_cmd;

	/************************
	 * 1. Allocating HW data
	 ************************/

	hw = ieee80211_alloc_hw(sizeof(struct il_priv), &il4965_mac_ops);
	if (!hw) {
		err = -ENOMEM;
		goto out;
	}
	il = hw->priv;
	il->hw = hw;
	SET_IEEE80211_DEV(hw, &pdev->dev);

	D_INFO("*** LOAD DRIVER ***\n");
	il->cfg = cfg;
	il->ops = &il4965_ops;
#ifdef CONFIG_IWLEGACY_DEBUGFS
	il->debugfs_ops = &il4965_debugfs_ops;
#endif
	il->pci_dev = pdev;
	il->inta_mask = CSR_INI_SET_MASK;

	/**************************
	 * 2. Initializing PCI bus
	 **************************/
	pci_disable_link_state(pdev,
			       PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1 |
			       PCIE_LINK_STATE_CLKPM);

	if (pci_enable_device(pdev)) {
		err = -ENODEV;
		goto out_ieee80211_free_hw;
	}

	pci_set_master(pdev);

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(36));
	if (err) {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		/* both attempts failed: */
		if (err) {
			IL_WARN("No suitable DMA available.\n");
			goto out_pci_disable_device;
		}
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err)
		goto out_pci_disable_device;

	pci_set_drvdata(pdev, il);

	/***********************
	 * 3. Read REV register
	 ***********************/
	il->hw_base = pci_ioremap_bar(pdev, 0);
	if (!il->hw_base) {
		err = -ENODEV;
		goto out_pci_release_regions;
	}

	D_INFO("pci_resource_len = 0x%08llx\n",
	       (unsigned long long)pci_resource_len(pdev, 0));
	D_INFO("pci_resource_base = %p\n", il->hw_base);

	/* these spin locks will be used in apm_ops.init and EEPROM access
	 * we should init now
	 */
	spin_lock_init(&il->reg_lock);
	spin_lock_init(&il->lock);

	/*
	 * stop and reset the on-board processor just in case it is in a
	 * strange state ... like being left stranded by a primary kernel
	 * and this is now the kdump kernel trying to start up
	 */
	_il_wr(il, CSR_RESET, CSR_RESET_REG_FLAG_NEVO_RESET);

	il4965_hw_detect(il);
	IL_INFO("Detected %s, REV=0x%X\n", il->cfg->name, il->hw_rev);

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	il4965_prepare_card_hw(il);
	if (!il->hw_ready) {
		IL_WARN("Failed, HW not ready\n");
		err = -EIO;
		goto out_iounmap;
	}

	/*****************
	 * 4. Read EEPROM
	 *****************/
	/* Read the EEPROM */
	err = il_eeprom_init(il);
	if (err) {
		IL_ERR("Unable to init EEPROM\n");
		goto out_iounmap;
	}
	err = il4965_eeprom_check_version(il);
	if (err)
		goto out_free_eeprom;

	/* extract MAC Address */
	il4965_eeprom_get_mac(il, il->addresses[0].addr);
	D_INFO("MAC address: %pM\n", il->addresses[0].addr);
	il->hw->wiphy->addresses = il->addresses;
	il->hw->wiphy->n_addresses = 1;

	/************************
	 * 5. Setup HW constants
	 ************************/
	il4965_set_hw_params(il);

	/*******************
	 * 6. Setup il
	 *******************/

	err = il4965_init_drv(il);
	if (err)
		goto out_free_eeprom;
	/* At this point both hw and il are initialized. */

	/********************
	 * 7. Setup services
	 ********************/
	spin_lock_irqsave(&il->lock, flags);
	il_disable_interrupts(il);
	spin_unlock_irqrestore(&il->lock, flags);

	pci_enable_msi(il->pci_dev);

	err = request_irq(il->pci_dev->irq, il_isr, IRQF_SHARED, DRV_NAME, il);
	if (err) {
		IL_ERR("Error allocating IRQ %d\n", il->pci_dev->irq);
		goto out_disable_msi;
	}

	il4965_setup_deferred_work(il);
	il4965_setup_handlers(il);

	/*********************************************
	 * 8. Enable interrupts and read RFKILL state
	 *********************************************/

	/* enable rfkill interrupt: hw bug w/a */
	pci_read_config_word(il->pci_dev, PCI_COMMAND, &pci_cmd);
	if (pci_cmd & PCI_COMMAND_INTX_DISABLE) {
		pci_cmd &= ~PCI_COMMAND_INTX_DISABLE;
		pci_write_config_word(il->pci_dev, PCI_COMMAND, pci_cmd);
	}

	il_enable_rfkill_int(il);

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (_il_rd(il, CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(S_RFKILL, &il->status);
	else
		set_bit(S_RFKILL, &il->status);

	wiphy_rfkill_set_hw_state(il->hw->wiphy,
				  test_bit(S_RFKILL, &il->status));

	il_power_initialize(il);

	init_completion(&il->_4965.firmware_loading_complete);

	err = il4965_request_firmware(il, true);
	if (err)
		goto out_destroy_workqueue;

	return 0;

out_destroy_workqueue:
	destroy_workqueue(il->workqueue);
	il->workqueue = NULL;
	free_irq(il->pci_dev->irq, il);
out_disable_msi:
	pci_disable_msi(il->pci_dev);
	il4965_uninit_drv(il);
out_free_eeprom:
	il_eeprom_free(il);
out_iounmap:
	iounmap(il->hw_base);
out_pci_release_regions:
	pci_release_regions(pdev);
out_pci_disable_device:
	pci_disable_device(pdev);
out_ieee80211_free_hw:
	ieee80211_free_hw(il->hw);
out:
	return err;
}

static void
il4965_pci_remove(struct pci_dev *pdev)
{
	struct il_priv *il = pci_get_drvdata(pdev);
	unsigned long flags;

	if (!il)
		return;

	wait_for_completion(&il->_4965.firmware_loading_complete);

	D_INFO("*** UNLOAD DRIVER ***\n");

	il_dbgfs_unregister(il);
	sysfs_remove_group(&pdev->dev.kobj, &il_attribute_group);

	/* ieee80211_unregister_hw call wil cause il_mac_stop to
	 * to be called and il4965_down since we are removing the device
	 * we need to set S_EXIT_PENDING bit.
	 */
	set_bit(S_EXIT_PENDING, &il->status);

	il_leds_exit(il);

	if (il->mac80211_registered) {
		ieee80211_unregister_hw(il->hw);
		il->mac80211_registered = 0;
	} else {
		il4965_down(il);
	}

	/*
	 * Make sure device is reset to low power before unloading driver.
	 * This may be redundant with il4965_down(), but there are paths to
	 * run il4965_down() without calling apm_ops.stop(), and there are
	 * paths to avoid running il4965_down() at all before leaving driver.
	 * This (inexpensive) call *makes sure* device is reset.
	 */
	il_apm_stop(il);

	/* make sure we flush any pending irq or
	 * tasklet for the driver
	 */
	spin_lock_irqsave(&il->lock, flags);
	il_disable_interrupts(il);
	spin_unlock_irqrestore(&il->lock, flags);

	il4965_synchronize_irq(il);

	il4965_dealloc_ucode_pci(il);

	if (il->rxq.bd)
		il4965_rx_queue_free(il, &il->rxq);
	il4965_hw_txq_ctx_free(il);

	il_eeprom_free(il);

	/*netif_stop_queue(dev); */

	/* ieee80211_unregister_hw calls il_mac_stop, which flushes
	 * il->workqueue... so we can't take down the workqueue
	 * until now... */
	destroy_workqueue(il->workqueue);
	il->workqueue = NULL;

	free_irq(il->pci_dev->irq, il);
	pci_disable_msi(il->pci_dev);
	iounmap(il->hw_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	il4965_uninit_drv(il);

	dev_kfree_skb(il->beacon_skb);

	ieee80211_free_hw(il->hw);
}

/*
 * Activate/Deactivate Tx DMA/FIFO channels according tx fifos mask
 * must be called under il->lock and mac access
 */
void
il4965_txq_set_sched(struct il_priv *il, u32 mask)
{
	il_wr_prph(il, IL49_SCD_TXFACT, mask);
}

/*****************************************************************************
 *
 * driver and module entry point
 *
 *****************************************************************************/

/* Hardware specific file defines the PCI IDs table for that hardware module */
static const struct pci_device_id il4965_hw_card_ids[] = {
	{IL_PCI_DEVICE(0x4229, PCI_ANY_ID, il4965_cfg)},
	{IL_PCI_DEVICE(0x4230, PCI_ANY_ID, il4965_cfg)},
	{0}
};
MODULE_DEVICE_TABLE(pci, il4965_hw_card_ids);

static struct pci_driver il4965_driver = {
	.name = DRV_NAME,
	.id_table = il4965_hw_card_ids,
	.probe = il4965_pci_probe,
	.remove = il4965_pci_remove,
	.driver.pm = IL_LEGACY_PM_OPS,
};

static int __init
il4965_init(void)
{

	int ret;
	pr_info(DRV_DESCRIPTION ", " DRV_VERSION "\n");
	pr_info(DRV_COPYRIGHT "\n");

	ret = il4965_rate_control_register();
	if (ret) {
		pr_err("Unable to register rate control algorithm: %d\n", ret);
		return ret;
	}

	ret = pci_register_driver(&il4965_driver);
	if (ret) {
		pr_err("Unable to initialize PCI module\n");
		goto error_register;
	}

	return ret;

error_register:
	il4965_rate_control_unregister();
	return ret;
}

static void __exit
il4965_exit(void)
{
	pci_unregister_driver(&il4965_driver);
	il4965_rate_control_unregister();
}

module_exit(il4965_exit);
module_init(il4965_init);

#ifdef CONFIG_IWLEGACY_DEBUG
module_param_named(debug, il_debug_level, uint, 0644);
MODULE_PARM_DESC(debug, "debug output mask");
#endif

module_param_named(swcrypto, il4965_mod_params.sw_crypto, int, 0444);
MODULE_PARM_DESC(swcrypto, "using crypto in software (default 0 [hardware])");
module_param_named(queues_num, il4965_mod_params.num_of_queues, int, 0444);
MODULE_PARM_DESC(queues_num, "number of hw queues.");
module_param_named(11n_disable, il4965_mod_params.disable_11n, int, 0444);
MODULE_PARM_DESC(11n_disable, "disable 11n functionality");
module_param_named(amsdu_size_8K, il4965_mod_params.amsdu_size_8K, int, 0444);
MODULE_PARM_DESC(amsdu_size_8K, "enable 8K amsdu size (default 0 [disabled])");
module_param_named(fw_restart, il4965_mod_params.restart_fw, int, 0444);
MODULE_PARM_DESC(fw_restart, "restart firmware in case of error");
