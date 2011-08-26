/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
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
#include <linux/pci-aspm.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include <net/mac80211.h>

#include <asm/div64.h>

#define DRV_NAME        "iwl4965"

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-sta.h"
#include "iwl-4965-calib.h"
#include "iwl-4965.h"
#include "iwl-4965-led.h"


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

void il4965_update_chain_flags(struct il_priv *il)
{
	struct il_rxon_context *ctx;

	if (il->cfg->ops->hcmd->set_rxon_chain) {
		for_each_context(il, ctx) {
			il->cfg->ops->hcmd->set_rxon_chain(il, ctx);
			if (ctx->active.rx_chain != ctx->staging.rx_chain)
				il_commit_rxon(il, ctx);
		}
	}
}

static void il4965_clear_free_frames(struct il_priv *il)
{
	struct list_head *element;

	D_INFO("%d frames on pre-allocated heap on clear.\n",
		       il->frames_count);

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

static struct il_frame *il4965_get_free_frame(struct il_priv *il)
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

static void il4965_free_frame(struct il_priv *il, struct il_frame *frame)
{
	memset(frame, 0, sizeof(*frame));
	list_add(&frame->list, &il->free_frames);
}

static u32 il4965_fill_beacon_frame(struct il_priv *il,
				 struct ieee80211_hdr *hdr,
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
static void il4965_set_beacon_tim(struct il_priv *il,
			       struct il_tx_beacon_cmd *tx_beacon_cmd,
			       u8 *beacon, u32 frame_size)
{
	u16 tim_idx;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)beacon;

	/*
	 * The index is relative to frame start but we start looking at the
	 * variable-length part of the beacon.
	 */
	tim_idx = mgmt->u.beacon.variable - beacon;

	/* Parse variable-length elements of beacon to find WLAN_EID_TIM */
	while ((tim_idx < (frame_size - 2)) &&
			(beacon[tim_idx] != WLAN_EID_TIM))
		tim_idx += beacon[tim_idx+1] + 2;

	/* If TIM field was found, set variables */
	if ((tim_idx < (frame_size - 1)) && (beacon[tim_idx] == WLAN_EID_TIM)) {
		tx_beacon_cmd->tim_idx = cpu_to_le16(tim_idx);
		tx_beacon_cmd->tim_size = beacon[tim_idx+1];
	} else
		IL_WARN("Unable to find TIM Element in beacon\n");
}

static unsigned int il4965_hw_get_beacon_cmd(struct il_priv *il,
				       struct il_frame *frame)
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

	if (!il->beacon_ctx) {
		IL_ERR("trying to build beacon w/o beacon context!\n");
		return 0;
	}

	/* Initialize memory */
	tx_beacon_cmd = &frame->u.beacon;
	memset(tx_beacon_cmd, 0, sizeof(*tx_beacon_cmd));

	/* Set up TX beacon contents */
	frame_size = il4965_fill_beacon_frame(il, tx_beacon_cmd->frame,
				sizeof(frame->u) - sizeof(*tx_beacon_cmd));
	if (WARN_ON_ONCE(frame_size > MAX_MPDU_SIZE))
		return 0;
	if (!frame_size)
		return 0;

	/* Set up TX command fields */
	tx_beacon_cmd->tx.len = cpu_to_le16((u16)frame_size);
	tx_beacon_cmd->tx.sta_id = il->beacon_ctx->bcast_sta_id;
	tx_beacon_cmd->tx.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;
	tx_beacon_cmd->tx.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK |
		TX_CMD_FLG_TSF_MSK | TX_CMD_FLG_STA_RATE_MSK;

	/* Set up TX beacon command fields */
	il4965_set_beacon_tim(il, tx_beacon_cmd, (u8 *)tx_beacon_cmd->frame,
			   frame_size);

	/* Set up packet rate and flags */
	rate = il_get_lowest_plcp(il, il->beacon_ctx);
	il->mgmt_tx_ant = il4965_toggle_tx_ant(il, il->mgmt_tx_ant,
					      il->hw_params.valid_tx_ant);
	rate_flags = il4965_ant_idx_to_flags(il->mgmt_tx_ant);
	if ((rate >= IL_FIRST_CCK_RATE) && (rate <= IL_LAST_CCK_RATE))
		rate_flags |= RATE_MCS_CCK_MSK;
	tx_beacon_cmd->tx.rate_n_flags = il4965_hw_set_rate_n_flags(rate,
			rate_flags);

	return sizeof(*tx_beacon_cmd) + frame_size;
}

int il4965_send_beacon_cmd(struct il_priv *il)
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

	rc = il_send_cmd_pdu(il, REPLY_TX_BEACON, frame_size,
			      &frame->u.cmd[0]);

	il4965_free_frame(il, frame);

	return rc;
}

static inline dma_addr_t il4965_tfd_tb_get_addr(struct il_tfd *tfd, u8 idx)
{
	struct il_tfd_tb *tb = &tfd->tbs[idx];

	dma_addr_t addr = get_unaligned_le32(&tb->lo);
	if (sizeof(dma_addr_t) > sizeof(u32))
		addr |=
		((dma_addr_t)(le16_to_cpu(tb->hi_n_len) & 0xF) << 16) << 16;

	return addr;
}

static inline u16 il4965_tfd_tb_get_len(struct il_tfd *tfd, u8 idx)
{
	struct il_tfd_tb *tb = &tfd->tbs[idx];

	return le16_to_cpu(tb->hi_n_len) >> 4;
}

static inline void il4965_tfd_set_tb(struct il_tfd *tfd, u8 idx,
				  dma_addr_t addr, u16 len)
{
	struct il_tfd_tb *tb = &tfd->tbs[idx];
	u16 hi_n_len = len << 4;

	put_unaligned_le32(addr, &tb->lo);
	if (sizeof(dma_addr_t) > sizeof(u32))
		hi_n_len |= ((addr >> 16) >> 16) & 0xF;

	tb->hi_n_len = cpu_to_le16(hi_n_len);

	tfd->num_tbs = idx + 1;
}

static inline u8 il4965_tfd_get_num_tbs(struct il_tfd *tfd)
{
	return tfd->num_tbs & 0x1f;
}

/**
 * il4965_hw_txq_free_tfd - Free all chunks referenced by TFD [txq->q.read_ptr]
 * @il - driver ilate data
 * @txq - tx queue
 *
 * Does NOT advance any TFD circular buffer read/write indexes
 * Does NOT free the TFD itself (which is within circular buffer)
 */
void il4965_hw_txq_free_tfd(struct il_priv *il, struct il_tx_queue *txq)
{
	struct il_tfd *tfd_tmp = (struct il_tfd *)txq->tfds;
	struct il_tfd *tfd;
	struct pci_dev *dev = il->pci_dev;
	int index = txq->q.read_ptr;
	int i;
	int num_tbs;

	tfd = &tfd_tmp[index];

	/* Sanity check on number of chunks */
	num_tbs = il4965_tfd_get_num_tbs(tfd);

	if (num_tbs >= IL_NUM_OF_TBS) {
		IL_ERR("Too many chunks: %i\n", num_tbs);
		/* @todo issue fatal error, it is quite serious situation */
		return;
	}

	/* Unmap tx_cmd */
	if (num_tbs)
		pci_unmap_single(dev,
				dma_unmap_addr(&txq->meta[index], mapping),
				dma_unmap_len(&txq->meta[index], len),
				PCI_DMA_BIDIRECTIONAL);

	/* Unmap chunks, if any. */
	for (i = 1; i < num_tbs; i++)
		pci_unmap_single(dev, il4965_tfd_tb_get_addr(tfd, i),
				il4965_tfd_tb_get_len(tfd, i),
				PCI_DMA_TODEVICE);

	/* free SKB */
	if (txq->txb) {
		struct sk_buff *skb;

		skb = txq->txb[txq->q.read_ptr].skb;

		/* can be called from irqs-disabled context */
		if (skb) {
			dev_kfree_skb_any(skb);
			txq->txb[txq->q.read_ptr].skb = NULL;
		}
	}
}

int il4965_hw_txq_attach_buf_to_tfd(struct il_priv *il,
				 struct il_tx_queue *txq,
				 dma_addr_t addr, u16 len,
				 u8 reset, u8 pad)
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
		IL_ERR("Unaligned address = %llx\n",
			  (unsigned long long)addr);

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
int il4965_hw_tx_queue_init(struct il_priv *il,
			 struct il_tx_queue *txq)
{
	int txq_id = txq->q.id;

	/* Circular buffer (TFD queue in DRAM) physical base address */
	il_wr(il, FH_MEM_CBBC_QUEUE(txq_id),
			     txq->q.dma_addr >> 8);

	return 0;
}

/******************************************************************************
 *
 * Generic RX handler implementations
 *
 ******************************************************************************/
static void il4965_rx_reply_alive(struct il_priv *il,
				struct il_rx_mem_buffer *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_alive_resp *palive;
	struct delayed_work *pwork;

	palive = &pkt->u.alive_frame;

	D_INFO("Alive ucode status 0x%08X revision "
		       "0x%01X 0x%01X\n",
		       palive->is_valid, palive->ver_type,
		       palive->ver_subtype);

	if (palive->ver_subtype == INITIALIZE_SUBTYPE) {
		D_INFO("Initialization Alive received.\n");
		memcpy(&il->card_alive_init,
		       &pkt->u.alive_frame,
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
		queue_delayed_work(il->workqueue, pwork,
				   msecs_to_jiffies(5));
	else
		IL_WARN("uCode did not respond OK.\n");
}

/**
 * il4965_bg_statistics_periodic - Timer callback to queue statistics
 *
 * This callback is provided in order to send a statistics request.
 *
 * This timer function is continually reset to execute within
 * REG_RECALIB_PERIOD seconds since the last STATISTICS_NOTIFICATION
 * was received.  We need to ensure we receive the statistics in order
 * to update the temperature used for calibrating the TXPOWER.
 */
static void il4965_bg_statistics_periodic(unsigned long data)
{
	struct il_priv *il = (struct il_priv *)data;

	if (test_bit(STATUS_EXIT_PENDING, &il->status))
		return;

	/* dont send host command if rf-kill is on */
	if (!il_is_ready_rf(il))
		return;

	il_send_statistics_request(il, CMD_ASYNC, false);
}

static void il4965_rx_beacon_notif(struct il_priv *il,
				struct il_rx_mem_buffer *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il4965_beacon_notif *beacon =
		(struct il4965_beacon_notif *)pkt->u.raw;
#ifdef CONFIG_IWLEGACY_DEBUG
	u8 rate = il4965_hw_get_rate(beacon->beacon_notify_hdr.rate_n_flags);

	D_RX("beacon status %x retries %d iss %d "
		"tsf %d %d rate %d\n",
		le32_to_cpu(beacon->beacon_notify_hdr.u.status) & TX_STATUS_MSK,
		beacon->beacon_notify_hdr.failure_frame,
		le32_to_cpu(beacon->ibss_mgr_status),
		le32_to_cpu(beacon->high_tsf),
		le32_to_cpu(beacon->low_tsf), rate);
#endif

	il->ibss_manager = le32_to_cpu(beacon->ibss_mgr_status);
}

static void il4965_perform_ct_kill_task(struct il_priv *il)
{
	unsigned long flags;

	D_POWER("Stop all queues\n");

	if (il->mac80211_registered)
		ieee80211_stop_queues(il->hw);

	_il_wr(il, CSR_UCODE_DRV_GP1_SET,
			CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
	_il_rd(il, CSR_UCODE_DRV_GP1);

	spin_lock_irqsave(&il->reg_lock, flags);
	if (!_il_grab_nic_access(il))
		_il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, flags);
}

/* Handle notification from uCode that card's power state is changing
 * due to software, hardware, or critical temperature RFKILL */
static void il4965_rx_card_state_notif(struct il_priv *il,
				    struct il_rx_mem_buffer *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	u32 flags = le32_to_cpu(pkt->u.card_state_notif.flags);
	unsigned long status = il->status;

	D_RF_KILL("Card state received: HW:%s SW:%s CT:%s\n",
			  (flags & HW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & SW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & CT_CARD_DISABLED) ?
			  "Reached" : "Not reached");

	if (flags & (SW_CARD_DISABLED | HW_CARD_DISABLED |
		     CT_CARD_DISABLED)) {

		_il_wr(il, CSR_UCODE_DRV_GP1_SET,
			    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

		il_wr(il, HBUS_TARG_MBX_C,
					HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED);

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
		set_bit(STATUS_RF_KILL_HW, &il->status);
	else
		clear_bit(STATUS_RF_KILL_HW, &il->status);

	if (!(flags & RXON_CARD_DISABLED))
		il_scan_cancel(il);

	if ((test_bit(STATUS_RF_KILL_HW, &status) !=
	     test_bit(STATUS_RF_KILL_HW, &il->status)))
		wiphy_rfkill_set_hw_state(il->hw->wiphy,
			test_bit(STATUS_RF_KILL_HW, &il->status));
	else
		wake_up(&il->wait_command_queue);
}

/**
 * il4965_setup_rx_handlers - Initialize Rx handler callbacks
 *
 * Setup the RX handlers for each of the reply types sent from the uCode
 * to the host.
 *
 * This function chains into the hardware specific files for them to setup
 * any hardware specific handlers as well.
 */
static void il4965_setup_rx_handlers(struct il_priv *il)
{
	il->rx_handlers[REPLY_ALIVE] = il4965_rx_reply_alive;
	il->rx_handlers[REPLY_ERROR] = il_rx_reply_error;
	il->rx_handlers[CHANNEL_SWITCH_NOTIFICATION] = il_rx_csa;
	il->rx_handlers[SPECTRUM_MEASURE_NOTIFICATION] =
			il_rx_spectrum_measure_notif;
	il->rx_handlers[PM_SLEEP_NOTIFICATION] = il_rx_pm_sleep_notif;
	il->rx_handlers[PM_DEBUG_STATISTIC_NOTIFIC] =
	    il_rx_pm_debug_statistics_notif;
	il->rx_handlers[BEACON_NOTIFICATION] = il4965_rx_beacon_notif;

	/*
	 * The same handler is used for both the REPLY to a discrete
	 * statistics request from the host as well as for the periodic
	 * statistics notifications (after received beacons) from the uCode.
	 */
	il->rx_handlers[REPLY_STATISTICS_CMD] = il4965_reply_statistics;
	il->rx_handlers[STATISTICS_NOTIFICATION] = il4965_rx_statistics;

	il_setup_rx_scan_handlers(il);

	/* status change handler */
	il->rx_handlers[CARD_STATE_NOTIFICATION] =
					il4965_rx_card_state_notif;

	il->rx_handlers[MISSED_BEACONS_NOTIFICATION] =
	    il4965_rx_missed_beacon_notif;
	/* Rx handlers */
	il->rx_handlers[REPLY_RX_PHY_CMD] = il4965_rx_reply_rx_phy;
	il->rx_handlers[REPLY_RX_MPDU_CMD] = il4965_rx_reply_rx;
	/* block ack */
	il->rx_handlers[REPLY_COMPRESSED_BA] = il4965_rx_reply_compressed_ba;
	/* Set up hardware specific Rx handlers */
	il->cfg->ops->lib->rx_handler_setup(il);
}

/**
 * il4965_rx_handle - Main entry function for receiving responses from uCode
 *
 * Uses the il->rx_handlers callback function array to invoke
 * the appropriate handlers, including command responses,
 * frame-received notifications, and other notifications.
 */
void il4965_rx_handle(struct il_priv *il)
{
	struct il_rx_mem_buffer *rxb;
	struct il_rx_pkt *pkt;
	struct il_rx_queue *rxq = &il->rxq;
	u32 r, i;
	int reclaim;
	unsigned long flags;
	u8 fill_rx = 0;
	u32 count = 8;
	int total_empty;

	/* uCode's read index (stored in shared DRAM) indicates the last Rx
	 * buffer that the driver may process (last buffer filled by ucode). */
	r = le16_to_cpu(rxq->rb_stts->closed_rb_num) &  0x0FFF;
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

		pci_unmap_page(il->pci_dev, rxb->page_dma,
			       PAGE_SIZE << il->hw_params.rx_page_order,
			       PCI_DMA_FROMDEVICE);
		pkt = rxb_addr(rxb);

		len = le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;
		len += sizeof(u32); /* account for status word */

		/* Reclaim a command buffer only if this packet is a response
		 *   to a (driver-originated) command.
		 * If the packet (e.g. Rx frame) originated from uCode,
		 *   there is no command buffer to reclaim.
		 * Ucode should set SEQ_RX_FRAME bit if ucode-originated,
		 *   but apparently a few don't get set; catch them here. */
		reclaim = !(pkt->hdr.sequence & SEQ_RX_FRAME) &&
			(pkt->hdr.cmd != REPLY_RX_PHY_CMD) &&
			(pkt->hdr.cmd != REPLY_RX) &&
			(pkt->hdr.cmd != REPLY_RX_MPDU_CMD) &&
			(pkt->hdr.cmd != REPLY_COMPRESSED_BA) &&
			(pkt->hdr.cmd != STATISTICS_NOTIFICATION) &&
			(pkt->hdr.cmd != REPLY_TX);

		/* Based on type of command response or notification,
		 *   handle those that need handling via function in
		 *   rx_handlers table.  See il4965_setup_rx_handlers() */
		if (il->rx_handlers[pkt->hdr.cmd]) {
			D_RX("r = %d, i = %d, %s, 0x%02x\n", r,
				i, il_get_cmd_string(pkt->hdr.cmd),
				pkt->hdr.cmd);
			il->isr_stats.rx_handlers[pkt->hdr.cmd]++;
			il->rx_handlers[pkt->hdr.cmd] (il, rxb);
		} else {
			/* No handling needed */
			D_RX(
				"r %d i %d No handler needed for %s, 0x%02x\n",
				r, i, il_get_cmd_string(pkt->hdr.cmd),
				pkt->hdr.cmd);
		}

		/*
		 * XXX: After here, we should always check rxb->page
		 * against NULL before touching it or its virtual
		 * memory (pkt). Because some rx_handler might have
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
			rxb->page_dma = pci_map_page(il->pci_dev, rxb->page,
				0, PAGE_SIZE << il->hw_params.rx_page_order,
				PCI_DMA_FROMDEVICE);
			list_add_tail(&rxb->list, &rxq->rx_free);
			rxq->free_count++;
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
static inline void il4965_synchronize_irq(struct il_priv *il)
{
	/* wait to make sure we flush pending tasklet*/
	synchronize_irq(il->pci_dev->irq);
	tasklet_kill(&il->irq_tasklet);
}

static void il4965_irq_tasklet(struct il_priv *il)
{
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
		D_ISR("inta 0x%08x, enabled 0x%08x, fh 0x%08x\n",
			      inta, inta_mask, inta_fh);
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
		if (!(_il_rd(il, CSR_GP_CNTRL) &
				CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW))
			hw_rf_kill = 1;

		IL_WARN("RF_KILL bit toggled to %s.\n",
				hw_rf_kill ? "disable radio" : "enable radio");

		il->isr_stats.rfkill++;

		/* driver only loads ucode once setting the interface up.
		 * the driver allows loading the ucode even if the radio
		 * is killed. Hence update the killswitch state here. The
		 * rfkill handler will care about restarting if needed.
		 */
		if (!test_bit(STATUS_ALIVE, &il->status)) {
			if (hw_rf_kill)
				set_bit(STATUS_RF_KILL_HW, &il->status);
			else
				clear_bit(STATUS_RF_KILL_HW, &il->status);
			wiphy_rfkill_set_hw_state(il->hw->wiphy, hw_rf_kill);
		}

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
		IL_ERR("Microcode SW error detected. "
			" Restarting 0x%X.\n", inta);
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
		IL_WARN("   with FH_INT = 0x%08x\n", inta_fh);
	}

	/* Re-enable all interrupts */
	/* only Re-enable if disabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &il->status))
		il_enable_interrupts(il);
	/* Re-enable RF_KILL if it occurred */
	else if (handled & CSR_INT_BIT_RF_KILL)
		il_enable_rfkill_int(il);

#ifdef CONFIG_IWLEGACY_DEBUG
	if (il_get_debug_level(il) & (IL_DL_ISR)) {
		inta = _il_rd(il, CSR_INT);
		inta_mask = _il_rd(il, CSR_INT_MASK);
		inta_fh = _il_rd(il, CSR_FH_INT_STATUS);
		D_ISR(
			"End inta 0x%08x, enabled 0x%08x, fh 0x%08x, "
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
static ssize_t il4965_show_debug_level(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct il_priv *il = dev_get_drvdata(d);
	return sprintf(buf, "0x%08X\n", il_get_debug_level(il));
}
static ssize_t il4965_store_debug_level(struct device *d,
				struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct il_priv *il = dev_get_drvdata(d);
	unsigned long val;
	int ret;

	ret = strict_strtoul(buf, 0, &val);
	if (ret)
		IL_ERR("%s is not in hex or decimal form.\n", buf);
	else {
		il->debug_level = val;
		if (il_alloc_traffic_mem(il))
			IL_ERR(
				"Not enough memory to generate traffic log\n");
	}
	return strnlen(buf, count);
}

static DEVICE_ATTR(debug_level, S_IWUSR | S_IRUGO,
			il4965_show_debug_level, il4965_store_debug_level);


#endif /* CONFIG_IWLEGACY_DEBUG */


static ssize_t il4965_show_temperature(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct il_priv *il = dev_get_drvdata(d);

	if (!il_is_alive(il))
		return -EAGAIN;

	return sprintf(buf, "%d\n", il->temperature);
}

static DEVICE_ATTR(temperature, S_IRUGO, il4965_show_temperature, NULL);

static ssize_t il4965_show_tx_power(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct il_priv *il = dev_get_drvdata(d);

	if (!il_is_ready_rf(il))
		return sprintf(buf, "off\n");
	else
		return sprintf(buf, "%d\n", il->tx_power_user_lmt);
}

static ssize_t il4965_store_tx_power(struct device *d,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct il_priv *il = dev_get_drvdata(d);
	unsigned long val;
	int ret;

	ret = strict_strtoul(buf, 10, &val);
	if (ret)
		IL_INFO("%s is not in decimal form.\n", buf);
	else {
		ret = il_set_tx_power(il, val, false);
		if (ret)
			IL_ERR("failed setting tx power (0x%d).\n",
				ret);
		else
			ret = count;
	}
	return ret;
}

static DEVICE_ATTR(tx_power, S_IWUSR | S_IRUGO,
			il4965_show_tx_power, il4965_store_tx_power);

static struct attribute *il_sysfs_entries[] = {
	&dev_attr_temperature.attr,
	&dev_attr_tx_power.attr,
#ifdef CONFIG_IWLEGACY_DEBUG
	&dev_attr_debug_level.attr,
#endif
	NULL
};

static struct attribute_group il_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = il_sysfs_entries,
};

/******************************************************************************
 *
 * uCode download functions
 *
 ******************************************************************************/

static void il4965_dealloc_ucode_pci(struct il_priv *il)
{
	il_free_fw_desc(il->pci_dev, &il->ucode_code);
	il_free_fw_desc(il->pci_dev, &il->ucode_data);
	il_free_fw_desc(il->pci_dev, &il->ucode_data_backup);
	il_free_fw_desc(il->pci_dev, &il->ucode_init);
	il_free_fw_desc(il->pci_dev, &il->ucode_init_data);
	il_free_fw_desc(il->pci_dev, &il->ucode_boot);
}

static void il4965_nic_start(struct il_priv *il)
{
	/* Remove all resets to allow NIC to operate */
	_il_wr(il, CSR_RESET, 0);
}

static void il4965_ucode_callback(const struct firmware *ucode_raw,
					void *context);
static int il4965_mac_setup_register(struct il_priv *il,
						u32 max_probe_length);

static int __must_check il4965_request_firmware(struct il_priv *il, bool first)
{
	const char *name_pre = il->cfg->fw_name_pre;
	char tag[8];

	if (first) {
		il->fw_index = il->cfg->ucode_api_max;
		sprintf(tag, "%d", il->fw_index);
	} else {
		il->fw_index--;
		sprintf(tag, "%d", il->fw_index);
	}

	if (il->fw_index < il->cfg->ucode_api_min) {
		IL_ERR("no suitable firmware found!\n");
		return -ENOENT;
	}

	sprintf(il->firmware_name, "%s%s%s", name_pre, tag, ".ucode");

	D_INFO("attempting to load firmware '%s'\n",
		       il->firmware_name);

	return request_firmware_nowait(THIS_MODULE, 1, il->firmware_name,
				       &il->pci_dev->dev, GFP_KERNEL, il,
				       il4965_ucode_callback);
}

struct il4965_firmware_pieces {
	const void *inst, *data, *init, *init_data, *boot;
	size_t inst_size, data_size, init_size, init_data_size, boot_size;
};

static int il4965_load_firmware(struct il_priv *il,
				       const struct firmware *ucode_raw,
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
		pieces->init_data_size =
				le32_to_cpu(ucode->v1.init_data_size);
		pieces->boot_size = le32_to_cpu(ucode->v1.boot_size);
		src = ucode->v1.data;
		break;
	}

	/* Verify size of file vs. image size info in file's header */
	if (ucode_raw->size != hdr_size + pieces->inst_size +
				pieces->data_size + pieces->init_size +
				pieces->init_data_size + pieces->boot_size) {

		IL_ERR(
			"uCode file size %d does not match expected size\n",
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

/**
 * il4965_ucode_callback - callback when firmware was loaded
 *
 * If loaded successfully, copies the firmware into buffers
 * for the card to fetch (via DMA).
 */
static void
il4965_ucode_callback(const struct firmware *ucode_raw, void *context)
{
	struct il_priv *il = context;
	struct il_ucode_header *ucode;
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
		if (il->fw_index <= il->cfg->ucode_api_max)
			IL_ERR(
				"request for firmware file '%s' failed.\n",
				il->firmware_name);
		goto try_again;
	}

	D_INFO("Loaded firmware file '%s' (%zd bytes).\n",
		       il->firmware_name, ucode_raw->size);

	/* Make sure that we got at least the API version number */
	if (ucode_raw->size < 4) {
		IL_ERR("File size way too small!\n");
		goto try_again;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (struct il_ucode_header *)ucode_raw->data;

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
		IL_ERR(
			"Driver unable to support your firmware API. "
			"Driver supports v%u, firmware is v%u.\n",
			api_max, api_ver);
		goto try_again;
	}

	if (api_ver != api_max)
		IL_ERR(
			"Firmware has old API version. Expected v%u, "
			"got v%u. New firmware can be obtained "
			"from http://www.intellinuxwireless.org.\n",
			api_max, api_ver);

	IL_INFO("loaded firmware version %u.%u.%u.%u\n",
		 IL_UCODE_MAJOR(il->ucode_ver),
		 IL_UCODE_MINOR(il->ucode_ver),
		 IL_UCODE_API(il->ucode_ver),
		 IL_UCODE_SERIAL(il->ucode_ver));

	snprintf(il->hw->wiphy->fw_version,
		 sizeof(il->hw->wiphy->fw_version),
		 "%u.%u.%u.%u",
		 IL_UCODE_MAJOR(il->ucode_ver),
		 IL_UCODE_MINOR(il->ucode_ver),
		 IL_UCODE_API(il->ucode_ver),
		 IL_UCODE_SERIAL(il->ucode_ver));

	/*
	 * For any of the failures below (before allocating pci memory)
	 * we will try to load a version with a smaller API -- maybe the
	 * user just got a corrupted version of the latest API.
	 */

	D_INFO("f/w package hdr ucode version raw = 0x%x\n",
		       il->ucode_ver);
	D_INFO("f/w package hdr runtime inst size = %Zd\n",
		       pieces.inst_size);
	D_INFO("f/w package hdr runtime data size = %Zd\n",
		       pieces.data_size);
	D_INFO("f/w package hdr init inst size = %Zd\n",
		       pieces.init_size);
	D_INFO("f/w package hdr init data size = %Zd\n",
		       pieces.init_data_size);
	D_INFO("f/w package hdr boot inst size = %Zd\n",
		       pieces.boot_size);

	/* Verify that uCode images will fit in card's SRAM */
	if (pieces.inst_size > il->hw_params.max_inst_size) {
		IL_ERR("uCode instr len %Zd too large to fit in\n",
			pieces.inst_size);
		goto try_again;
	}

	if (pieces.data_size > il->hw_params.max_data_size) {
		IL_ERR("uCode data len %Zd too large to fit in\n",
			pieces.data_size);
		goto try_again;
	}

	if (pieces.init_size > il->hw_params.max_inst_size) {
		IL_ERR("uCode init instr len %Zd too large to fit in\n",
			pieces.init_size);
		goto try_again;
	}

	if (pieces.init_data_size > il->hw_params.max_data_size) {
		IL_ERR("uCode init data len %Zd too large to fit in\n",
			pieces.init_data_size);
		goto try_again;
	}

	if (pieces.boot_size > il->hw_params.max_bsm_size) {
		IL_ERR("uCode boot instr len %Zd too large to fit in\n",
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
	D_INFO("Copying (but not loading) uCode instr len %Zd\n",
			pieces.inst_size);
	memcpy(il->ucode_code.v_addr, pieces.inst, pieces.inst_size);

	D_INFO("uCode instr buf vaddr = 0x%p, paddr = 0x%08x\n",
		il->ucode_code.v_addr, (u32)il->ucode_code.p_addr);

	/*
	 * Runtime data
	 * NOTE:  Copy into backup buffer will be done in il_up()
	 */
	D_INFO("Copying (but not loading) uCode data len %Zd\n",
			pieces.data_size);
	memcpy(il->ucode_data.v_addr, pieces.data, pieces.data_size);
	memcpy(il->ucode_data_backup.v_addr, pieces.data, pieces.data_size);

	/* Initialization instructions */
	if (pieces.init_size) {
		D_INFO(
				"Copying (but not loading) init instr len %Zd\n",
				pieces.init_size);
		memcpy(il->ucode_init.v_addr, pieces.init, pieces.init_size);
	}

	/* Initialization data */
	if (pieces.init_data_size) {
		D_INFO(
				"Copying (but not loading) init data len %Zd\n",
			       pieces.init_data_size);
		memcpy(il->ucode_init_data.v_addr, pieces.init_data,
		       pieces.init_data_size);
	}

	/* Bootstrap instructions */
	D_INFO("Copying (but not loading) boot instr len %Zd\n",
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

	err = il_dbgfs_register(il, DRV_NAME);
	if (err)
		IL_ERR(
		"failed to create debugfs files. Ignoring error: %d\n", err);

	err = sysfs_create_group(&il->pci_dev->dev.kobj,
					&il_attribute_group);
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

static const char * const desc_lookup_text[] = {
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
	"VCC_NOT_STABLE",
	"FH_ERROR",
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

static struct { char *name; u8 num; } advanced_lookup[] = {
	{ "NMI_INTERRUPT_WDG", 0x34 },
	{ "SYSASSERT", 0x35 },
	{ "UCODE_VERSION_MISMATCH", 0x37 },
	{ "BAD_COMMAND", 0x38 },
	{ "NMI_INTERRUPT_DATA_ACTION_PT", 0x3C },
	{ "FATAL_ERROR", 0x3D },
	{ "NMI_TRM_HW_ERR", 0x46 },
	{ "NMI_INTERRUPT_TRM", 0x4C },
	{ "NMI_INTERRUPT_BREAK_POINT", 0x54 },
	{ "NMI_INTERRUPT_WDG_RXF_FULL", 0x5C },
	{ "NMI_INTERRUPT_WDG_NO_RBD_RXF_FULL", 0x64 },
	{ "NMI_INTERRUPT_HOST", 0x66 },
	{ "NMI_INTERRUPT_ACTION_PT", 0x7C },
	{ "NMI_INTERRUPT_UNKNOWN", 0x84 },
	{ "NMI_INTERRUPT_INST_ACTION_PT", 0x86 },
	{ "ADVANCED_SYSASSERT", 0 },
};

static const char *il4965_desc_lookup(u32 num)
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

void il4965_dump_nic_error_log(struct il_priv *il)
{
	u32 data2, line;
	u32 desc, time, count, base, data1;
	u32 blink1, blink2, ilink1, ilink2;
	u32 pc, hcmd;

	if (il->ucode_type == UCODE_INIT) {
		base = le32_to_cpu(il->card_alive_init.error_event_table_ptr);
	} else {
		base = le32_to_cpu(il->card_alive.error_event_table_ptr);
	}

	if (!il->cfg->ops->lib->is_valid_rtc_data_addr(base)) {
		IL_ERR(
			"Not valid error log pointer 0x%08X for %s uCode\n",
			base, (il->ucode_type == UCODE_INIT) ? "Init" : "RT");
		return;
	}

	count = il_read_targ_mem(il, base);

	if (ERROR_START_OFFSET <= count * ERROR_ELEM_SIZE) {
		IL_ERR("Start IWL Error Log Dump:\n");
		IL_ERR("Status: 0x%08lX, count: %d\n",
			il->status, count);
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
	IL_ERR("0x%05X 0x%05X 0x%05X 0x%05X 0x%05X 0x%05X\n",
		pc, blink1, blink2, ilink1, ilink2, hcmd);
}

static void il4965_rf_kill_ct_config(struct il_priv *il)
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

	ret = il_send_cmd_pdu(il, REPLY_CT_KILL_CONFIG_CMD,
			       sizeof(cmd), &cmd);
	if (ret)
		IL_ERR("REPLY_CT_KILL_CONFIG_CMD failed\n");
	else
		D_INFO("REPLY_CT_KILL_CONFIG_CMD "
				"succeeded, "
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

static int il4965_alive_notify(struct il_priv *il)
{
	u32 a;
	unsigned long flags;
	int i, chan;
	u32 reg_val;

	spin_lock_irqsave(&il->lock, flags);

	/* Clear 4965's internal Tx Scheduler data base */
	il->scd_base_addr = il_rd_prph(il,
					IL49_SCD_SRAM_BASE_ADDR);
	a = il->scd_base_addr + IL49_SCD_CONTEXT_DATA_OFFSET;
	for (; a < il->scd_base_addr + IL49_SCD_TX_STTS_BITMAP_OFFSET; a += 4)
		il_write_targ_mem(il, a, 0);
	for (; a < il->scd_base_addr + IL49_SCD_TRANSLATE_TBL_OFFSET; a += 4)
		il_write_targ_mem(il, a, 0);
	for (; a < il->scd_base_addr +
	       IL49_SCD_TRANSLATE_TBL_OFFSET_QUEUE(il->hw_params.max_txq_num); a += 4)
		il_write_targ_mem(il, a, 0);

	/* Tel 4965 where to find Tx byte count tables */
	il_wr_prph(il, IL49_SCD_DRAM_BASE_ADDR,
			il->scd_bc_tbls.dma >> 10);

	/* Enable DMA channel */
	for (chan = 0; chan < FH49_TCSR_CHNL_NUM ; chan++)
		il_wr(il,
				FH_TCSR_CHNL_TX_CONFIG_REG(chan),
				FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
				FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);

	/* Update FH chicken bits */
	reg_val = il_rd(il, FH_TX_CHICKEN_BITS_REG);
	il_wr(il, FH_TX_CHICKEN_BITS_REG,
			   reg_val | FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN);

	/* Disable chain mode for all queues */
	il_wr_prph(il, IL49_SCD_QUEUECHAIN_SEL, 0);

	/* Initialize each Tx queue (including the command queue) */
	for (i = 0; i < il->hw_params.max_txq_num; i++) {

		/* TFD circular buffer read/write indexes */
		il_wr_prph(il, IL49_SCD_QUEUE_RDPTR(i), 0);
		il_wr(il, HBUS_TARG_WRPTR, 0 | (i << 8));

		/* Max Tx Window size for Scheduler-ACK mode */
		il_write_targ_mem(il, il->scd_base_addr +
				IL49_SCD_CONTEXT_QUEUE_OFFSET(i),
				(SCD_WIN_SIZE <<
				IL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_POS) &
				IL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_MSK);

		/* Frame limit */
		il_write_targ_mem(il, il->scd_base_addr +
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

/**
 * il4965_alive_start - called after REPLY_ALIVE notification received
 *                   from protocol/runtime uCode (initialization uCode's
 *                   Alive gets handled by il_init_alive_start()).
 */
static void il4965_alive_start(struct il_priv *il)
{
	int ret = 0;
	struct il_rxon_context *ctx = &il->contexts[IL_RXON_CTX_BSS];

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
		IL_WARN(
			"Could not complete ALIVE transition [ntf]: %d\n", ret);
		goto restart;
	}


	/* After the ALIVE response, we can send host commands to the uCode */
	set_bit(STATUS_ALIVE, &il->status);

	/* Enable watchdog to monitor the driver tx queues */
	il_setup_watchdog(il);

	if (il_is_rfkill(il))
		return;

	ieee80211_wake_queues(il->hw);

	il->active_rate = IL_RATES_MASK;

	if (il_is_associated_ctx(ctx)) {
		struct il_rxon_cmd *active_rxon =
				(struct il_rxon_cmd *)&ctx->active;
		/* apply any changes in staging */
		ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	} else {
		struct il_rxon_context *tmp;
		/* Initialize our rx_config data */
		for_each_context(il, tmp)
			il_connection_init_rx_config(il, tmp);

		if (il->cfg->ops->hcmd->set_rxon_chain)
			il->cfg->ops->hcmd->set_rxon_chain(il, ctx);
	}

	/* Configure bluetooth coexistence if enabled */
	il_send_bt_config(il);

	il4965_reset_run_time_calib(il);

	set_bit(STATUS_READY, &il->status);

	/* Configure the adapter for unassociated operation */
	il_commit_rxon(il, ctx);

	/* At this point, the NIC is initialized and operational */
	il4965_rf_kill_ct_config(il);

	D_INFO("ALIVE processing complete.\n");
	wake_up(&il->wait_command_queue);

	il_power_update_mode(il, true);
	D_INFO("Updated power mode\n");

	return;

 restart:
	queue_work(il->workqueue, &il->restart);
}

static void il4965_cancel_deferred_work(struct il_priv *il);

static void __il4965_down(struct il_priv *il)
{
	unsigned long flags;
	int exit_pending;

	D_INFO(DRV_NAME " is going down\n");

	il_scan_cancel_timeout(il, 200);

	exit_pending = test_and_set_bit(STATUS_EXIT_PENDING, &il->status);

	/* Stop TX queues watchdog. We need to have STATUS_EXIT_PENDING bit set
	 * to prevent rearm timer */
	del_timer_sync(&il->watchdog);

	il_clear_ucode_stations(il, NULL);
	il_dealloc_bcast_stations(il);
	il_clear_driver_stations(il);

	/* Unblock any waiting calls */
	wake_up_all(&il->wait_command_queue);

	/* Wipe out the EXIT_PENDING status bit if we are not actually
	 * exiting the module */
	if (!exit_pending)
		clear_bit(STATUS_EXIT_PENDING, &il->status);

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
		il->status = test_bit(STATUS_RF_KILL_HW, &il->status) <<
					STATUS_RF_KILL_HW |
			       test_bit(STATUS_GEO_CONFIGURED, &il->status) <<
					STATUS_GEO_CONFIGURED |
			       test_bit(STATUS_EXIT_PENDING, &il->status) <<
					STATUS_EXIT_PENDING;
		goto exit;
	}

	/* ...otherwise clear out all the status bits but the RF Kill
	 * bit and continue taking the NIC down. */
	il->status &= test_bit(STATUS_RF_KILL_HW, &il->status) <<
				STATUS_RF_KILL_HW |
			test_bit(STATUS_GEO_CONFIGURED, &il->status) <<
				STATUS_GEO_CONFIGURED |
			test_bit(STATUS_FW_ERROR, &il->status) <<
				STATUS_FW_ERROR |
		       test_bit(STATUS_EXIT_PENDING, &il->status) <<
				STATUS_EXIT_PENDING;

	il4965_txq_ctx_stop(il);
	il4965_rxq_stop(il);

	/* Power-down device's busmaster DMA clocks */
	il_wr_prph(il, APMG_CLK_DIS_REG, APMG_CLK_VAL_DMA_CLK_RQT);
	udelay(5);

	/* Make sure (redundant) we've released our request to stay awake */
	il_clear_bit(il, CSR_GP_CNTRL,
				CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	/* Stop the device, and put it in low power state */
	il_apm_stop(il);

 exit:
	memset(&il->card_alive, 0, sizeof(struct il_alive_resp));

	dev_kfree_skb(il->beacon_skb);
	il->beacon_skb = NULL;

	/* clear out any free frames */
	il4965_clear_free_frames(il);
}

static void il4965_down(struct il_priv *il)
{
	mutex_lock(&il->mutex);
	__il4965_down(il);
	mutex_unlock(&il->mutex);

	il4965_cancel_deferred_work(il);
}

#define HW_READY_TIMEOUT (50)

static int il4965_set_hw_ready(struct il_priv *il)
{
	int ret = 0;

	il_set_bit(il, CSR_HW_IF_CONFIG_REG,
		CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	/* See if we got it */
	ret = _il_poll_bit(il, CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
				CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
				HW_READY_TIMEOUT);
	if (ret != -ETIMEDOUT)
		il->hw_ready = true;
	else
		il->hw_ready = false;

	D_INFO("hardware %s\n",
		      (il->hw_ready == 1) ? "ready" : "not ready");
	return ret;
}

static int il4965_prepare_card_hw(struct il_priv *il)
{
	int ret = 0;

	D_INFO("il4965_prepare_card_hw enter\n");

	ret = il4965_set_hw_ready(il);
	if (il->hw_ready)
		return ret;

	/* If HW is not ready, prepare the conditions to check again */
	il_set_bit(il, CSR_HW_IF_CONFIG_REG,
			CSR_HW_IF_CONFIG_REG_PREPARE);

	ret = _il_poll_bit(il, CSR_HW_IF_CONFIG_REG,
			~CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE,
			CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE, 150000);

	/* HW should be ready by now, check again. */
	if (ret != -ETIMEDOUT)
		il4965_set_hw_ready(il);

	return ret;
}

#define MAX_HW_RESTARTS 5

static int __il4965_up(struct il_priv *il)
{
	struct il_rxon_context *ctx;
	int i;
	int ret;

	if (test_bit(STATUS_EXIT_PENDING, &il->status)) {
		IL_WARN("Exit pending; will not bring the NIC up\n");
		return -EIO;
	}

	if (!il->ucode_data_backup.v_addr || !il->ucode_data.v_addr) {
		IL_ERR("ucode not available for device bringup\n");
		return -EIO;
	}

	for_each_context(il, ctx) {
		ret = il4965_alloc_bcast_station(il, ctx);
		if (ret) {
			il_dealloc_bcast_stations(il);
			return ret;
		}
	}

	il4965_prepare_card_hw(il);

	if (!il->hw_ready) {
		IL_WARN("Exit HW not ready\n");
		return -EIO;
	}

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (_il_rd(il,
		CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &il->status);
	else
		set_bit(STATUS_RF_KILL_HW, &il->status);

	if (il_is_rfkill(il)) {
		wiphy_rfkill_set_hw_state(il->hw->wiphy, true);

		il_enable_interrupts(il);
		IL_WARN("Radio disabled by HW RF Kill switch\n");
		return 0;
	}

	_il_wr(il, CSR_INT, 0xFFFFFFFF);

	/* must be initialised before il_hw_nic_init */
	il->cmd_queue = IL_DEFAULT_CMD_QUEUE_NUM;

	ret = il4965_hw_nic_init(il);
	if (ret) {
		IL_ERR("Unable to init nic\n");
		return ret;
	}

	/* make sure rfkill handshake bits are cleared */
	_il_wr(il, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	_il_wr(il, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

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
		ret = il->cfg->ops->lib->load_ucode(il);

		if (ret) {
			IL_ERR("Unable to set up bootstrap uCode: %d\n",
				ret);
			continue;
		}

		/* start card; "initialize" will load runtime ucode */
		il4965_nic_start(il);

		D_INFO(DRV_NAME " is coming up\n");

		return 0;
	}

	set_bit(STATUS_EXIT_PENDING, &il->status);
	__il4965_down(il);
	clear_bit(STATUS_EXIT_PENDING, &il->status);

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

static void il4965_bg_init_alive_start(struct work_struct *data)
{
	struct il_priv *il =
	    container_of(data, struct il_priv, init_alive_start.work);

	mutex_lock(&il->mutex);
	if (test_bit(STATUS_EXIT_PENDING, &il->status))
		goto out;

	il->cfg->ops->lib->init_alive_start(il);
out:
	mutex_unlock(&il->mutex);
}

static void il4965_bg_alive_start(struct work_struct *data)
{
	struct il_priv *il =
	    container_of(data, struct il_priv, alive_start.work);

	mutex_lock(&il->mutex);
	if (test_bit(STATUS_EXIT_PENDING, &il->status))
		goto out;

	il4965_alive_start(il);
out:
	mutex_unlock(&il->mutex);
}

static void il4965_bg_run_time_calib_work(struct work_struct *work)
{
	struct il_priv *il = container_of(work, struct il_priv,
			run_time_calib_work);

	mutex_lock(&il->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &il->status) ||
	    test_bit(STATUS_SCANNING, &il->status)) {
		mutex_unlock(&il->mutex);
		return;
	}

	if (il->start_calib) {
		il4965_chain_noise_calibration(il,
				(void *)&il->_4965.statistics);
		il4965_sensitivity_calibration(il,
				(void *)&il->_4965.statistics);
	}

	mutex_unlock(&il->mutex);
}

static void il4965_bg_restart(struct work_struct *data)
{
	struct il_priv *il = container_of(data, struct il_priv, restart);

	if (test_bit(STATUS_EXIT_PENDING, &il->status))
		return;

	if (test_and_clear_bit(STATUS_FW_ERROR, &il->status)) {
		struct il_rxon_context *ctx;

		mutex_lock(&il->mutex);
		for_each_context(il, ctx)
			ctx->vif = NULL;
		il->is_open = 0;

		__il4965_down(il);

		mutex_unlock(&il->mutex);
		il4965_cancel_deferred_work(il);
		ieee80211_restart_hw(il->hw);
	} else {
		il4965_down(il);

		mutex_lock(&il->mutex);
		if (test_bit(STATUS_EXIT_PENDING, &il->status)) {
			mutex_unlock(&il->mutex);
			return;
		}

		__il4965_up(il);
		mutex_unlock(&il->mutex);
	}
}

static void il4965_bg_rx_replenish(struct work_struct *data)
{
	struct il_priv *il =
	    container_of(data, struct il_priv, rx_replenish);

	if (test_bit(STATUS_EXIT_PENDING, &il->status))
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
static int il4965_mac_setup_register(struct il_priv *il,
				  u32 max_probe_length)
{
	int ret;
	struct ieee80211_hw *hw = il->hw;
	struct il_rxon_context *ctx;

	hw->rate_control_algorithm = "iwl-4965-rs";

	/* Tell mac80211 our characteristics */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_AMPDU_AGGREGATION |
		    IEEE80211_HW_NEED_DTIM_PERIOD |
		    IEEE80211_HW_SPECTRUM_MGMT |
		    IEEE80211_HW_REPORTS_TX_ACK_STATUS;

	if (il->cfg->sku & IL_SKU_N)
		hw->flags |= IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS |
			     IEEE80211_HW_SUPPORTS_STATIC_SMPS;

	hw->sta_data_size = sizeof(struct il_station_priv);
	hw->vif_data_size = sizeof(struct il_vif_priv);

	for_each_context(il, ctx) {
		hw->wiphy->interface_modes |= ctx->interface_modes;
		hw->wiphy->interface_modes |= ctx->exclusive_interface_modes;
	}

	hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY |
			    WIPHY_FLAG_DISABLE_BEACON_HINTS;

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

	if (il->bands[IEEE80211_BAND_2GHZ].n_channels)
		il->hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&il->bands[IEEE80211_BAND_2GHZ];
	if (il->bands[IEEE80211_BAND_5GHZ].n_channels)
		il->hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&il->bands[IEEE80211_BAND_5GHZ];

	il_leds_init(il);

	ret = ieee80211_register_hw(il->hw);
	if (ret) {
		IL_ERR("Failed to register hw (error %d)\n", ret);
		return ret;
	}
	il->mac80211_registered = 1;

	return 0;
}


int il4965_mac_start(struct ieee80211_hw *hw)
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
			test_bit(STATUS_READY, &il->status),
			UCODE_READY_TIMEOUT);
	if (!ret) {
		if (!test_bit(STATUS_READY, &il->status)) {
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

void il4965_mac_stop(struct ieee80211_hw *hw)
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

void il4965_mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct il_priv *il = hw->priv;

	D_MACDUMP("enter\n");

	D_TX("dev->xmit(%d bytes) at rate 0x%02x\n", skb->len,
		     ieee80211_get_tx_rate(hw, IEEE80211_SKB_CB(skb))->bitrate);

	if (il4965_tx_skb(il, skb))
		dev_kfree_skb_any(skb);

	D_MACDUMP("leave\n");
}

void il4965_mac_update_tkip_key(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_key_conf *keyconf,
				struct ieee80211_sta *sta,
				u32 iv32, u16 *phase1key)
{
	struct il_priv *il = hw->priv;
	struct il_vif_priv *vif_priv = (void *)vif->drv_priv;

	D_MAC80211("enter\n");

	il4965_update_tkip_key(il, vif_priv->ctx, keyconf, sta,
			    iv32, phase1key);

	D_MAC80211("leave\n");
}

int il4965_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		       struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		       struct ieee80211_key_conf *key)
{
	struct il_priv *il = hw->priv;
	struct il_vif_priv *vif_priv = (void *)vif->drv_priv;
	struct il_rxon_context *ctx = vif_priv->ctx;
	int ret;
	u8 sta_id;
	bool is_default_wep_key = false;

	D_MAC80211("enter\n");

	if (il->cfg->mod_params->sw_crypto) {
		D_MAC80211("leave - hwcrypto disabled\n");
		return -EOPNOTSUPP;
	}

	sta_id = il_sta_id_or_broadcast(il, vif_priv->ctx, sta);
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
	     key->cipher == WLAN_CIPHER_SUITE_WEP104) &&
	    !sta) {
		if (cmd == SET_KEY)
			is_default_wep_key = !ctx->key_mapping_keys;
		else
			is_default_wep_key =
					(key->hw_key_idx == HW_KEY_DEFAULT);
	}

	switch (cmd) {
	case SET_KEY:
		if (is_default_wep_key)
			ret = il4965_set_default_wep_key(il,
							vif_priv->ctx, key);
		else
			ret = il4965_set_dynamic_key(il, vif_priv->ctx,
						  key, sta_id);

		D_MAC80211("enable hwcrypto key\n");
		break;
	case DISABLE_KEY:
		if (is_default_wep_key)
			ret = il4965_remove_default_wep_key(il, ctx, key);
		else
			ret = il4965_remove_dynamic_key(il, ctx,
							key, sta_id);

		D_MAC80211("disable hwcrypto key\n");
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&il->mutex);
	D_MAC80211("leave\n");

	return ret;
}

int il4965_mac_ampdu_action(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    enum ieee80211_ampdu_mlme_action action,
			    struct ieee80211_sta *sta, u16 tid, u16 *ssn,
			    u8 buf_size)
{
	struct il_priv *il = hw->priv;
	int ret = -EINVAL;

	D_HT("A-MPDU action on addr %pM tid %d\n",
		     sta->addr, tid);

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
		if (test_bit(STATUS_EXIT_PENDING, &il->status))
			ret = 0;
		break;
	case IEEE80211_AMPDU_TX_START:
		D_HT("start Tx\n");
		ret = il4965_tx_agg_start(il, vif, sta, tid, ssn);
		break;
	case IEEE80211_AMPDU_TX_STOP:
		D_HT("stop Tx\n");
		ret = il4965_tx_agg_stop(il, vif, sta, tid);
		if (test_bit(STATUS_EXIT_PENDING, &il->status))
			ret = 0;
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ret = 0;
		break;
	}
	mutex_unlock(&il->mutex);

	return ret;
}

int il4965_mac_sta_add(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct il_priv *il = hw->priv;
	struct il_station_priv *sta_priv = (void *)sta->drv_priv;
	struct il_vif_priv *vif_priv = (void *)vif->drv_priv;
	bool is_ap = vif->type == NL80211_IFTYPE_STATION;
	int ret;
	u8 sta_id;

	D_INFO("received request to add station %pM\n",
			sta->addr);
	mutex_lock(&il->mutex);
	D_INFO("proceeding to add station %pM\n",
			sta->addr);
	sta_priv->common.sta_id = IL_INVALID_STATION;

	atomic_set(&sta_priv->pending_frames, 0);

	ret = il_add_station_common(il, vif_priv->ctx, sta->addr,
				     is_ap, sta, &sta_id);
	if (ret) {
		IL_ERR("Unable to add station %pM (%d)\n",
			sta->addr, ret);
		/* Should we return success if return code is EEXIST ? */
		mutex_unlock(&il->mutex);
		return ret;
	}

	sta_priv->common.sta_id = sta_id;

	/* Initialize rate scaling */
	D_INFO("Initializing rate scaling for station %pM\n",
		       sta->addr);
	il4965_rs_rate_init(il, sta, sta_id);
	mutex_unlock(&il->mutex);

	return 0;
}

void il4965_mac_channel_switch(struct ieee80211_hw *hw,
			       struct ieee80211_channel_switch *ch_switch)
{
	struct il_priv *il = hw->priv;
	const struct il_channel_info *ch_info;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_channel *channel = ch_switch->channel;
	struct il_ht_config *ht_conf = &il->current_ht_config;

	struct il_rxon_context *ctx = &il->contexts[IL_RXON_CTX_BSS];
	u16 ch;

	D_MAC80211("enter\n");

	mutex_lock(&il->mutex);

	if (il_is_rfkill(il))
		goto out;

	if (test_bit(STATUS_EXIT_PENDING, &il->status) ||
	    test_bit(STATUS_SCANNING, &il->status) ||
	    test_bit(STATUS_CHANNEL_SWITCH_PENDING, &il->status))
		goto out;

	if (!il_is_associated_ctx(ctx))
		goto out;

	if (!il->cfg->ops->lib->set_channel_switch)
		goto out;

	ch = channel->hw_value;
	if (le16_to_cpu(ctx->active.channel) == ch)
		goto out;

	ch_info = il_get_channel_info(il, channel->band, ch);
	if (!il_is_channel_valid(ch_info)) {
		D_MAC80211("invalid channel\n");
		goto out;
	}

	spin_lock_irq(&il->lock);

	il->current_ht_config.smps = conf->smps_mode;

	/* Configure HT40 channels */
	ctx->ht.enabled = conf_is_ht(conf);
	if (ctx->ht.enabled) {
		if (conf_is_ht40_minus(conf)) {
			ctx->ht.extension_chan_offset =
				IEEE80211_HT_PARAM_CHA_SEC_BELOW;
			ctx->ht.is_40mhz = true;
		} else if (conf_is_ht40_plus(conf)) {
			ctx->ht.extension_chan_offset =
				IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
			ctx->ht.is_40mhz = true;
		} else {
			ctx->ht.extension_chan_offset =
				IEEE80211_HT_PARAM_CHA_SEC_NONE;
			ctx->ht.is_40mhz = false;
		}
	} else
		ctx->ht.is_40mhz = false;

	if ((le16_to_cpu(ctx->staging.channel) != ch))
		ctx->staging.flags = 0;

	il_set_rxon_channel(il, channel, ctx);
	il_set_rxon_ht(il, ht_conf);
	il_set_flags_for_band(il, ctx, channel->band, ctx->vif);

	spin_unlock_irq(&il->lock);

	il_set_rate(il);
	/*
	 * at this point, staging_rxon has the
	 * configuration for channel switch
	 */
	set_bit(STATUS_CHANNEL_SWITCH_PENDING, &il->status);
	il->switch_channel = cpu_to_le16(ch);
	if (il->cfg->ops->lib->set_channel_switch(il, ch_switch)) {
		clear_bit(STATUS_CHANNEL_SWITCH_PENDING, &il->status);
		il->switch_channel = 0;
		ieee80211_chswitch_done(ctx->vif, false);
	}

out:
	mutex_unlock(&il->mutex);
	D_MAC80211("leave\n");
}

void il4965_configure_filter(struct ieee80211_hw *hw,
			     unsigned int changed_flags,
			     unsigned int *total_flags,
			     u64 multicast)
{
	struct il_priv *il = hw->priv;
	__le32 filter_or = 0, filter_nand = 0;
	struct il_rxon_context *ctx;

#define CHK(test, flag)	do { \
	if (*total_flags & (test))		\
		filter_or |= (flag);		\
	else					\
		filter_nand |= (flag);		\
	} while (0)

	D_MAC80211("Enter: changed: 0x%x, total: 0x%x\n",
			changed_flags, *total_flags);

	CHK(FIF_OTHER_BSS | FIF_PROMISC_IN_BSS, RXON_FILTER_PROMISC_MSK);
	/* Setting _just_ RXON_FILTER_CTL2HOST_MSK causes FH errors */
	CHK(FIF_CONTROL, RXON_FILTER_CTL2HOST_MSK | RXON_FILTER_PROMISC_MSK);
	CHK(FIF_BCN_PRBRESP_PROMISC, RXON_FILTER_BCON_AWARE_MSK);

#undef CHK

	mutex_lock(&il->mutex);

	for_each_context(il, ctx) {
		ctx->staging.filter_flags &= ~filter_nand;
		ctx->staging.filter_flags |= filter_or;

		/*
		 * Not committing directly because hardware can perform a scan,
		 * but we'll eventually commit the filter flags change anyway.
		 */
	}

	mutex_unlock(&il->mutex);

	/*
	 * Receiving all multicast frames is always enabled by the
	 * default flags setup in il_connection_init_rx_config()
	 * since we currently do not support programming multicast
	 * filters into the device.
	 */
	*total_flags &= FIF_OTHER_BSS | FIF_ALLMULTI | FIF_PROMISC_IN_BSS |
			FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL;
}

/*****************************************************************************
 *
 * driver setup and teardown
 *
 *****************************************************************************/

static void il4965_bg_txpower_work(struct work_struct *work)
{
	struct il_priv *il = container_of(work, struct il_priv,
			txpower_work);

	mutex_lock(&il->mutex);

	/* If a scan happened to start before we got here
	 * then just return; the statistics notification will
	 * kick off another scheduled work to compensate for
	 * any temperature delta we missed here. */
	if (test_bit(STATUS_EXIT_PENDING, &il->status) ||
	    test_bit(STATUS_SCANNING, &il->status))
		goto out;

	/* Regardless of if we are associated, we must reconfigure the
	 * TX power since frames can be sent on non-radar channels while
	 * not associated */
	il->cfg->ops->lib->send_tx_power(il);

	/* Update last_temperature to keep is_calib_needed from running
	 * when it isn't needed... */
	il->last_temperature = il->temperature;
out:
	mutex_unlock(&il->mutex);
}

static void il4965_setup_deferred_work(struct il_priv *il)
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

	init_timer(&il->statistics_periodic);
	il->statistics_periodic.data = (unsigned long)il;
	il->statistics_periodic.function = il4965_bg_statistics_periodic;

	init_timer(&il->watchdog);
	il->watchdog.data = (unsigned long)il;
	il->watchdog.function = il_bg_watchdog;

	tasklet_init(&il->irq_tasklet, (void (*)(unsigned long))
		il4965_irq_tasklet, (unsigned long)il);
}

static void il4965_cancel_deferred_work(struct il_priv *il)
{
	cancel_work_sync(&il->txpower_work);
	cancel_delayed_work_sync(&il->init_alive_start);
	cancel_delayed_work(&il->alive_start);
	cancel_work_sync(&il->run_time_calib_work);

	il_cancel_scan_deferred_work(il);

	del_timer_sync(&il->statistics_periodic);
}

static void il4965_init_hw_rates(struct il_priv *il,
			      struct ieee80211_rate *rates)
{
	int i;

	for (i = 0; i < IL_RATE_COUNT_LEGACY; i++) {
		rates[i].bitrate = il_rates[i].ieee * 5;
		rates[i].hw_value = i; /* Rate scaling will work on indexes */
		rates[i].hw_value_short = i;
		rates[i].flags = 0;
		if ((i >= IL_FIRST_CCK_RATE) && (i <= IL_LAST_CCK_RATE)) {
			/*
			 * If CCK != 1M then set short preamble rate flag.
			 */
			rates[i].flags |=
				(il_rates[i].plcp == IL_RATE_1M_PLCP) ?
					0 : IEEE80211_RATE_SHORT_PREAMBLE;
		}
	}
}
/*
 * Acquire il->lock before calling this function !
 */
void il4965_set_wr_ptrs(struct il_priv *il, int txq_id, u32 index)
{
	il_wr(il, HBUS_TARG_WRPTR,
			     (index & 0xff) | (txq_id << 8));
	il_wr_prph(il, IL49_SCD_QUEUE_RDPTR(txq_id), index);
}

void il4965_tx_queue_set_status(struct il_priv *il,
					struct il_tx_queue *txq,
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

	D_INFO("%s %s Queue %d on AC %d\n",
		       active ? "Activate" : "Deactivate",
		       scd_retry ? "BA" : "AC", txq_id, tx_fifo_id);
}


static int il4965_init_drv(struct il_priv *il)
{
	int ret;

	spin_lock_init(&il->sta_lock);
	spin_lock_init(&il->hcmd_lock);

	INIT_LIST_HEAD(&il->free_frames);

	mutex_init(&il->mutex);

	il->ieee_channels = NULL;
	il->ieee_rates = NULL;
	il->band = IEEE80211_BAND_2GHZ;

	il->iw_mode = NL80211_IFTYPE_STATION;
	il->current_ht_config.smps = IEEE80211_SMPS_STATIC;
	il->missed_beacon_threshold = IL_MISSED_BEACON_THRESHOLD_DEF;

	/* initialize force reset */
	il->force_reset.reset_duration = IL_DELAY_NEXT_FORCE_FW_RELOAD;

	/* Choose which receivers/antennas to use */
	if (il->cfg->ops->hcmd->set_rxon_chain)
		il->cfg->ops->hcmd->set_rxon_chain(il,
					&il->contexts[IL_RXON_CTX_BSS]);

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

static void il4965_uninit_drv(struct il_priv *il)
{
	il4965_calib_free_results(il);
	il_free_geos(il);
	il_free_channel_map(il);
	kfree(il->scan_cmd);
}

static void il4965_hw_detect(struct il_priv *il)
{
	il->hw_rev = _il_rd(il, CSR_HW_REV);
	il->hw_wa_rev = _il_rd(il, CSR_HW_REV_WA_REG);
	il->rev_id = il->pci_dev->revision;
	D_INFO("HW Revision ID = 0x%X\n", il->rev_id);
}

static int il4965_set_hw_params(struct il_priv *il)
{
	il->hw_params.max_rxq_size = RX_QUEUE_SIZE;
	il->hw_params.max_rxq_log = RX_QUEUE_SIZE_LOG;
	if (il->cfg->mod_params->amsdu_size_8K)
		il->hw_params.rx_page_order = get_order(IL_RX_BUF_SIZE_8K);
	else
		il->hw_params.rx_page_order = get_order(IL_RX_BUF_SIZE_4K);

	il->hw_params.max_beacon_itrvl = IL_MAX_UCODE_BEACON_INTERVAL;

	if (il->cfg->mod_params->disable_11n)
		il->cfg->sku &= ~IL_SKU_N;

	/* Device-specific setup */
	return il->cfg->ops->lib->set_hw_params(il);
}

static const u8 il4965_bss_ac_to_fifo[] = {
	IL_TX_FIFO_VO,
	IL_TX_FIFO_VI,
	IL_TX_FIFO_BE,
	IL_TX_FIFO_BK,
};

static const u8 il4965_bss_ac_to_queue[] = {
	0, 1, 2, 3,
};

static int
il4965_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = 0, i;
	struct il_priv *il;
	struct ieee80211_hw *hw;
	struct il_cfg *cfg = (struct il_cfg *)(ent->driver_data);
	unsigned long flags;
	u16 pci_cmd;

	/************************
	 * 1. Allocating HW data
	 ************************/

	hw = il_alloc_all(cfg);
	if (!hw) {
		err = -ENOMEM;
		goto out;
	}
	il = hw->priv;
	/* At this point both hw and il are allocated. */

	/*
	 * The default context is always valid,
	 * more may be discovered when firmware
	 * is loaded.
	 */
	il->valid_contexts = BIT(IL_RXON_CTX_BSS);

	for (i = 0; i < NUM_IL_RXON_CTX; i++)
		il->contexts[i].ctxid = i;

	il->contexts[IL_RXON_CTX_BSS].always_active = true;
	il->contexts[IL_RXON_CTX_BSS].is_active = true;
	il->contexts[IL_RXON_CTX_BSS].rxon_cmd = REPLY_RXON;
	il->contexts[IL_RXON_CTX_BSS].rxon_timing_cmd = REPLY_RXON_TIMING;
	il->contexts[IL_RXON_CTX_BSS].rxon_assoc_cmd = REPLY_RXON_ASSOC;
	il->contexts[IL_RXON_CTX_BSS].qos_cmd = REPLY_QOS_PARAM;
	il->contexts[IL_RXON_CTX_BSS].ap_sta_id = IL_AP_ID;
	il->contexts[IL_RXON_CTX_BSS].wep_key_cmd = REPLY_WEPKEY;
	il->contexts[IL_RXON_CTX_BSS].ac_to_fifo = il4965_bss_ac_to_fifo;
	il->contexts[IL_RXON_CTX_BSS].ac_to_queue = il4965_bss_ac_to_queue;
	il->contexts[IL_RXON_CTX_BSS].exclusive_interface_modes =
		BIT(NL80211_IFTYPE_ADHOC);
	il->contexts[IL_RXON_CTX_BSS].interface_modes =
		BIT(NL80211_IFTYPE_STATION);
	il->contexts[IL_RXON_CTX_BSS].ap_devtype = RXON_DEV_TYPE_AP;
	il->contexts[IL_RXON_CTX_BSS].ibss_devtype = RXON_DEV_TYPE_IBSS;
	il->contexts[IL_RXON_CTX_BSS].station_devtype = RXON_DEV_TYPE_ESS;
	il->contexts[IL_RXON_CTX_BSS].unused_devtype = RXON_DEV_TYPE_ESS;

	BUILD_BUG_ON(NUM_IL_RXON_CTX != 1);

	SET_IEEE80211_DEV(hw, &pdev->dev);

	D_INFO("*** LOAD DRIVER ***\n");
	il->cfg = cfg;
	il->pci_dev = pdev;
	il->inta_mask = CSR_INI_SET_MASK;

	if (il_alloc_traffic_mem(il))
		IL_ERR("Not enough memory to generate traffic log\n");

	/**************************
	 * 2. Initializing PCI bus
	 **************************/
	pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1 |
				PCIE_LINK_STATE_CLKPM);

	if (pci_enable_device(pdev)) {
		err = -ENODEV;
		goto out_ieee80211_free_hw;
	}

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(36));
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(36));
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (!err)
			err = pci_set_consistent_dma_mask(pdev,
							DMA_BIT_MASK(32));
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
	il->hw_base = pci_iomap(pdev, 0, 0);
	if (!il->hw_base) {
		err = -ENODEV;
		goto out_pci_release_regions;
	}

	D_INFO("pci_resource_len = 0x%08llx\n",
		(unsigned long long) pci_resource_len(pdev, 0));
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
	IL_INFO("Detected %s, REV=0x%X\n",
		il->cfg->name, il->hw_rev);

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	il4965_prepare_card_hw(il);
	if (!il->hw_ready) {
		IL_WARN("Failed, HW not ready\n");
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
	if (il4965_set_hw_params(il)) {
		IL_ERR("failed to set hw parameters\n");
		goto out_free_eeprom;
	}

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

	err = request_irq(il->pci_dev->irq, il_isr,
			  IRQF_SHARED, DRV_NAME, il);
	if (err) {
		IL_ERR("Error allocating IRQ %d\n", il->pci_dev->irq);
		goto out_disable_msi;
	}

	il4965_setup_deferred_work(il);
	il4965_setup_rx_handlers(il);

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
	if (_il_rd(il, CSR_GP_CNTRL) &
		CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &il->status);
	else
		set_bit(STATUS_RF_KILL_HW, &il->status);

	wiphy_rfkill_set_hw_state(il->hw->wiphy,
		test_bit(STATUS_RF_KILL_HW, &il->status));

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
	pci_iounmap(pdev, il->hw_base);
 out_pci_release_regions:
	pci_set_drvdata(pdev, NULL);
	pci_release_regions(pdev);
 out_pci_disable_device:
	pci_disable_device(pdev);
 out_ieee80211_free_hw:
	il_free_traffic_mem(il);
	ieee80211_free_hw(il->hw);
 out:
	return err;
}

static void __devexit il4965_pci_remove(struct pci_dev *pdev)
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
	 * we need to set STATUS_EXIT_PENDING bit.
	 */
	set_bit(STATUS_EXIT_PENDING, &il->status);

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
	flush_workqueue(il->workqueue);

	/* ieee80211_unregister_hw calls il_mac_stop, which flushes
	 * il->workqueue... so we can't take down the workqueue
	 * until now... */
	destroy_workqueue(il->workqueue);
	il->workqueue = NULL;
	il_free_traffic_mem(il);

	free_irq(il->pci_dev->irq, il);
	pci_disable_msi(il->pci_dev);
	pci_iounmap(pdev, il->hw_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	il4965_uninit_drv(il);

	dev_kfree_skb(il->beacon_skb);

	ieee80211_free_hw(il->hw);
}

/*
 * Activate/Deactivate Tx DMA/FIFO channels according tx fifos mask
 * must be called under il->lock and mac access
 */
void il4965_txq_set_sched(struct il_priv *il, u32 mask)
{
	il_wr_prph(il, IL49_SCD_TXFACT, mask);
}

/*****************************************************************************
 *
 * driver and module entry point
 *
 *****************************************************************************/

/* Hardware specific file defines the PCI IDs table for that hardware module */
static DEFINE_PCI_DEVICE_TABLE(il4965_hw_card_ids) = {
	{IL_PCI_DEVICE(0x4229, PCI_ANY_ID, il4965_cfg)},
	{IL_PCI_DEVICE(0x4230, PCI_ANY_ID, il4965_cfg)},
	{0}
};
MODULE_DEVICE_TABLE(pci, il4965_hw_card_ids);

static struct pci_driver il4965_driver = {
	.name = DRV_NAME,
	.id_table = il4965_hw_card_ids,
	.probe = il4965_pci_probe,
	.remove = __devexit_p(il4965_pci_remove),
	.driver.pm = IL_LEGACY_PM_OPS,
};

static int __init il4965_init(void)
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

static void __exit il4965_exit(void)
{
	pci_unregister_driver(&il4965_driver);
	il4965_rate_control_unregister();
}

module_exit(il4965_exit);
module_init(il4965_init);

#ifdef CONFIG_IWLEGACY_DEBUG
module_param_named(debug, il_debug_level, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "debug output mask");
#endif

module_param_named(swcrypto, il4965_mod_params.sw_crypto, int, S_IRUGO);
MODULE_PARM_DESC(swcrypto, "using crypto in software (default 0 [hardware])");
module_param_named(queues_num, il4965_mod_params.num_of_queues, int, S_IRUGO);
MODULE_PARM_DESC(queues_num, "number of hw queues.");
module_param_named(11n_disable, il4965_mod_params.disable_11n, int, S_IRUGO);
MODULE_PARM_DESC(11n_disable, "disable 11n functionality");
module_param_named(amsdu_size_8K, il4965_mod_params.amsdu_size_8K,
		   int, S_IRUGO);
MODULE_PARM_DESC(amsdu_size_8K, "enable 8K amsdu size");
module_param_named(fw_restart, il4965_mod_params.restart_fw, int, S_IRUGO);
MODULE_PARM_DESC(fw_restart, "restart firmware in case of error");
