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
#include <linux/wireless.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include <net/mac80211.h>

#include <asm/div64.h>

#define DRV_NAME        "iwlagn"

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-sta.h"
#include "iwl-agn-calib.h"
#include "iwl-agn.h"


/******************************************************************************
 *
 * module boiler plate
 *
 ******************************************************************************/

/*
 * module name, copyright, version, etc.
 */
#define DRV_DESCRIPTION	"Intel(R) Wireless WiFi Link AGN driver for Linux"

#ifdef CONFIG_IWLWIFI_DEBUG
#define VD "d"
#else
#define VD
#endif

#define DRV_VERSION     IWLWIFI_VERSION VD


MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");

static int iwlagn_ant_coupling;
static bool iwlagn_bt_ch_announce = 1;

void iwl_update_chain_flags(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;

	if (priv->cfg->ops->hcmd->set_rxon_chain) {
		for_each_context(priv, ctx) {
			priv->cfg->ops->hcmd->set_rxon_chain(priv, ctx);
			if (ctx->active.rx_chain != ctx->staging.rx_chain)
				iwlcore_commit_rxon(priv, ctx);
		}
	}
}

/* Parse the beacon frame to find the TIM element and set tim_idx & tim_size */
static void iwl_set_beacon_tim(struct iwl_priv *priv,
			       struct iwl_tx_beacon_cmd *tx_beacon_cmd,
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
		IWL_WARN(priv, "Unable to find TIM Element in beacon\n");
}

int iwlagn_send_beacon_cmd(struct iwl_priv *priv)
{
	struct iwl_tx_beacon_cmd *tx_beacon_cmd;
	struct iwl_host_cmd cmd = {
		.id = REPLY_TX_BEACON,
	};
	u32 frame_size;
	u32 rate_flags;
	u32 rate;

	/*
	 * We have to set up the TX command, the TX Beacon command, and the
	 * beacon contents.
	 */

	lockdep_assert_held(&priv->mutex);

	if (!priv->beacon_ctx) {
		IWL_ERR(priv, "trying to build beacon w/o beacon context!\n");
		return 0;
	}

	if (WARN_ON(!priv->beacon_skb))
		return -EINVAL;

	/* Allocate beacon command */
	if (!priv->beacon_cmd)
		priv->beacon_cmd = kzalloc(sizeof(*tx_beacon_cmd), GFP_KERNEL);
	tx_beacon_cmd = priv->beacon_cmd;
	if (!tx_beacon_cmd)
		return -ENOMEM;

	frame_size = priv->beacon_skb->len;

	/* Set up TX command fields */
	tx_beacon_cmd->tx.len = cpu_to_le16((u16)frame_size);
	tx_beacon_cmd->tx.sta_id = priv->beacon_ctx->bcast_sta_id;
	tx_beacon_cmd->tx.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;
	tx_beacon_cmd->tx.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK |
		TX_CMD_FLG_TSF_MSK | TX_CMD_FLG_STA_RATE_MSK;

	/* Set up TX beacon command fields */
	iwl_set_beacon_tim(priv, tx_beacon_cmd, priv->beacon_skb->data,
			   frame_size);

	/* Set up packet rate and flags */
	rate = iwl_rate_get_lowest_plcp(priv, priv->beacon_ctx);
	priv->mgmt_tx_ant = iwl_toggle_tx_ant(priv, priv->mgmt_tx_ant,
					      priv->hw_params.valid_tx_ant);
	rate_flags = iwl_ant_idx_to_flags(priv->mgmt_tx_ant);
	if ((rate >= IWL_FIRST_CCK_RATE) && (rate <= IWL_LAST_CCK_RATE))
		rate_flags |= RATE_MCS_CCK_MSK;
	tx_beacon_cmd->tx.rate_n_flags = iwl_hw_set_rate_n_flags(rate,
			rate_flags);

	/* Submit command */
	cmd.len[0] = sizeof(*tx_beacon_cmd);
	cmd.data[0] = tx_beacon_cmd;
	cmd.dataflags[0] = IWL_HCMD_DFL_NOCOPY;
	cmd.len[1] = frame_size;
	cmd.data[1] = priv->beacon_skb->data;
	cmd.dataflags[1] = IWL_HCMD_DFL_NOCOPY;

	return iwl_send_cmd_sync(priv, &cmd);
}

static void iwl_bg_beacon_update(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, beacon_update);
	struct sk_buff *beacon;

	mutex_lock(&priv->mutex);
	if (!priv->beacon_ctx) {
		IWL_ERR(priv, "updating beacon w/o beacon context!\n");
		goto out;
	}

	if (priv->beacon_ctx->vif->type != NL80211_IFTYPE_AP) {
		/*
		 * The ucode will send beacon notifications even in
		 * IBSS mode, but we don't want to process them. But
		 * we need to defer the type check to here due to
		 * requiring locking around the beacon_ctx access.
		 */
		goto out;
	}

	/* Pull updated AP beacon from mac80211. will fail if not in AP mode */
	beacon = ieee80211_beacon_get(priv->hw, priv->beacon_ctx->vif);
	if (!beacon) {
		IWL_ERR(priv, "update beacon failed -- keeping old\n");
		goto out;
	}

	/* new beacon skb is allocated every time; dispose previous.*/
	dev_kfree_skb(priv->beacon_skb);

	priv->beacon_skb = beacon;

	iwlagn_send_beacon_cmd(priv);
 out:
	mutex_unlock(&priv->mutex);
}

static void iwl_bg_bt_runtime_config(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, bt_runtime_config);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* dont send host command if rf-kill is on */
	if (!iwl_is_ready_rf(priv))
		return;
	priv->cfg->ops->hcmd->send_bt_config(priv);
}

static void iwl_bg_bt_full_concurrency(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, bt_full_concurrency);
	struct iwl_rxon_context *ctx;

	mutex_lock(&priv->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		goto out;

	/* dont send host command if rf-kill is on */
	if (!iwl_is_ready_rf(priv))
		goto out;

	IWL_DEBUG_INFO(priv, "BT coex in %s mode\n",
		       priv->bt_full_concurrent ?
		       "full concurrency" : "3-wire");

	/*
	 * LQ & RXON updated cmds must be sent before BT Config cmd
	 * to avoid 3-wire collisions
	 */
	for_each_context(priv, ctx) {
		if (priv->cfg->ops->hcmd->set_rxon_chain)
			priv->cfg->ops->hcmd->set_rxon_chain(priv, ctx);
		iwlcore_commit_rxon(priv, ctx);
	}

	priv->cfg->ops->hcmd->send_bt_config(priv);
out:
	mutex_unlock(&priv->mutex);
}

/**
 * iwl_bg_statistics_periodic - Timer callback to queue statistics
 *
 * This callback is provided in order to send a statistics request.
 *
 * This timer function is continually reset to execute within
 * REG_RECALIB_PERIOD seconds since the last STATISTICS_NOTIFICATION
 * was received.  We need to ensure we receive the statistics in order
 * to update the temperature used for calibrating the TXPOWER.
 */
static void iwl_bg_statistics_periodic(unsigned long data)
{
	struct iwl_priv *priv = (struct iwl_priv *)data;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* dont send host command if rf-kill is on */
	if (!iwl_is_ready_rf(priv))
		return;

	iwl_send_statistics_request(priv, CMD_ASYNC, false);
}


static void iwl_print_cont_event_trace(struct iwl_priv *priv, u32 base,
					u32 start_idx, u32 num_events,
					u32 mode)
{
	u32 i;
	u32 ptr;        /* SRAM byte address of log data */
	u32 ev, time, data; /* event log data */
	unsigned long reg_flags;

	if (mode == 0)
		ptr = base + (4 * sizeof(u32)) + (start_idx * 2 * sizeof(u32));
	else
		ptr = base + (4 * sizeof(u32)) + (start_idx * 3 * sizeof(u32));

	/* Make sure device is powered up for SRAM reads */
	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	if (iwl_grab_nic_access(priv)) {
		spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
		return;
	}

	/* Set starting address; reads will auto-increment */
	iwl_write32(priv, HBUS_TARG_MEM_RADDR, ptr);
	rmb();

	/*
	 * "time" is actually "data" for mode 0 (no timestamp).
	 * place event id # at far right for easier visual parsing.
	 */
	for (i = 0; i < num_events; i++) {
		ev = iwl_read32(priv, HBUS_TARG_MEM_RDAT);
		time = iwl_read32(priv, HBUS_TARG_MEM_RDAT);
		if (mode == 0) {
			trace_iwlwifi_dev_ucode_cont_event(priv,
							0, time, ev);
		} else {
			data = iwl_read32(priv, HBUS_TARG_MEM_RDAT);
			trace_iwlwifi_dev_ucode_cont_event(priv,
						time, data, ev);
		}
	}
	/* Allow device to power down */
	iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
}

static void iwl_continuous_event_trace(struct iwl_priv *priv)
{
	u32 capacity;   /* event log capacity in # entries */
	u32 base;       /* SRAM byte address of event log header */
	u32 mode;       /* 0 - no timestamp, 1 - timestamp recorded */
	u32 num_wraps;  /* # times uCode wrapped to top of log */
	u32 next_entry; /* index of next entry to be written by uCode */

	base = priv->device_pointers.error_event_table;
	if (priv->cfg->ops->lib->is_valid_rtc_data_addr(base)) {
		capacity = iwl_read_targ_mem(priv, base);
		num_wraps = iwl_read_targ_mem(priv, base + (2 * sizeof(u32)));
		mode = iwl_read_targ_mem(priv, base + (1 * sizeof(u32)));
		next_entry = iwl_read_targ_mem(priv, base + (3 * sizeof(u32)));
	} else
		return;

	if (num_wraps == priv->event_log.num_wraps) {
		iwl_print_cont_event_trace(priv,
				       base, priv->event_log.next_entry,
				       next_entry - priv->event_log.next_entry,
				       mode);
		priv->event_log.non_wraps_count++;
	} else {
		if ((num_wraps - priv->event_log.num_wraps) > 1)
			priv->event_log.wraps_more_count++;
		else
			priv->event_log.wraps_once_count++;
		trace_iwlwifi_dev_ucode_wrap_event(priv,
				num_wraps - priv->event_log.num_wraps,
				next_entry, priv->event_log.next_entry);
		if (next_entry < priv->event_log.next_entry) {
			iwl_print_cont_event_trace(priv, base,
			       priv->event_log.next_entry,
			       capacity - priv->event_log.next_entry,
			       mode);

			iwl_print_cont_event_trace(priv, base, 0,
				next_entry, mode);
		} else {
			iwl_print_cont_event_trace(priv, base,
			       next_entry, capacity - next_entry,
			       mode);

			iwl_print_cont_event_trace(priv, base, 0,
				next_entry, mode);
		}
	}
	priv->event_log.num_wraps = num_wraps;
	priv->event_log.next_entry = next_entry;
}

/**
 * iwl_bg_ucode_trace - Timer callback to log ucode event
 *
 * The timer is continually set to execute every
 * UCODE_TRACE_PERIOD milliseconds after the last timer expired
 * this function is to perform continuous uCode event logging operation
 * if enabled
 */
static void iwl_bg_ucode_trace(unsigned long data)
{
	struct iwl_priv *priv = (struct iwl_priv *)data;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (priv->event_log.ucode_trace) {
		iwl_continuous_event_trace(priv);
		/* Reschedule the timer to occur in UCODE_TRACE_PERIOD */
		mod_timer(&priv->ucode_trace,
			 jiffies + msecs_to_jiffies(UCODE_TRACE_PERIOD));
	}
}

static void iwl_bg_tx_flush(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, tx_flush);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* do nothing if rf-kill is on */
	if (!iwl_is_ready_rf(priv))
		return;

	if (priv->cfg->ops->lib->txfifo_flush) {
		IWL_DEBUG_INFO(priv, "device request: flush all tx frames\n");
		iwlagn_dev_txfifo_flush(priv, IWL_DROP_ALL);
	}
}

/**
 * iwl_rx_handle - Main entry function for receiving responses from uCode
 *
 * Uses the priv->rx_handlers callback function array to invoke
 * the appropriate handlers, including command responses,
 * frame-received notifications, and other notifications.
 */
static void iwl_rx_handle(struct iwl_priv *priv)
{
	struct iwl_rx_mem_buffer *rxb;
	struct iwl_rx_packet *pkt;
	struct iwl_rx_queue *rxq = &priv->rxq;
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
		IWL_DEBUG_RX(priv, "r = %d, i = %d\n", r, i);

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
		if (WARN_ON(rxb == NULL)) {
			i = (i + 1) & RX_QUEUE_MASK;
			continue;
		}

		rxq->queue[i] = NULL;

		pci_unmap_page(priv->pci_dev, rxb->page_dma,
			       PAGE_SIZE << priv->hw_params.rx_page_order,
			       PCI_DMA_FROMDEVICE);
		pkt = rxb_addr(rxb);

		len = le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;
		len += sizeof(u32); /* account for status word */
		trace_iwlwifi_dev_rx(priv, pkt, len);

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

		/*
		 * Do the notification wait before RX handlers so
		 * even if the RX handler consumes the RXB we have
		 * access to it in the notification wait entry.
		 */
		if (!list_empty(&priv->_agn.notif_waits)) {
			struct iwl_notification_wait *w;

			spin_lock(&priv->_agn.notif_wait_lock);
			list_for_each_entry(w, &priv->_agn.notif_waits, list) {
				if (w->cmd == pkt->hdr.cmd) {
					w->triggered = true;
					if (w->fn)
						w->fn(priv, pkt, w->fn_data);
				}
			}
			spin_unlock(&priv->_agn.notif_wait_lock);

			wake_up_all(&priv->_agn.notif_waitq);
		}
		if (priv->pre_rx_handler)
			priv->pre_rx_handler(priv, rxb);

		/* Based on type of command response or notification,
		 *   handle those that need handling via function in
		 *   rx_handlers table.  See iwl_setup_rx_handlers() */
		if (priv->rx_handlers[pkt->hdr.cmd]) {
			IWL_DEBUG_RX(priv, "r = %d, i = %d, %s, 0x%02x\n", r,
				i, get_cmd_string(pkt->hdr.cmd), pkt->hdr.cmd);
			priv->isr_stats.rx_handlers[pkt->hdr.cmd]++;
			priv->rx_handlers[pkt->hdr.cmd] (priv, rxb);
		} else {
			/* No handling needed */
			IWL_DEBUG_RX(priv,
				"r %d i %d No handler needed for %s, 0x%02x\n",
				r, i, get_cmd_string(pkt->hdr.cmd),
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
			 * and fire off the (possibly) blocking iwl_send_cmd()
			 * as we reclaim the driver command queue */
			if (rxb->page)
				iwl_tx_cmd_complete(priv, rxb);
			else
				IWL_WARN(priv, "Claim null rxb?\n");
		}

		/* Reuse the page if possible. For notification packets and
		 * SKBs that fail to Rx correctly, add them back into the
		 * rx_free list for reuse later. */
		spin_lock_irqsave(&rxq->lock, flags);
		if (rxb->page != NULL) {
			rxb->page_dma = pci_map_page(priv->pci_dev, rxb->page,
				0, PAGE_SIZE << priv->hw_params.rx_page_order,
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
				iwlagn_rx_replenish_now(priv);
				count = 0;
			}
		}
	}

	/* Backtrack one entry */
	rxq->read = i;
	if (fill_rx)
		iwlagn_rx_replenish_now(priv);
	else
		iwlagn_rx_queue_restock(priv);
}

/* tasklet for iwlagn interrupt */
static void iwl_irq_tasklet(struct iwl_priv *priv)
{
	u32 inta = 0;
	u32 handled = 0;
	unsigned long flags;
	u32 i;
#ifdef CONFIG_IWLWIFI_DEBUG
	u32 inta_mask;
#endif

	spin_lock_irqsave(&priv->lock, flags);

	/* Ack/clear/reset pending uCode interrupts.
	 * Note:  Some bits in CSR_INT are "OR" of bits in CSR_FH_INT_STATUS,
	 */
	/* There is a hardware bug in the interrupt mask function that some
	 * interrupts (i.e. CSR_INT_BIT_SCD) can still be generated even if
	 * they are disabled in the CSR_INT_MASK register. Furthermore the
	 * ICT interrupt handling mechanism has another bug that might cause
	 * these unmasked interrupts fail to be detected. We workaround the
	 * hardware bugs here by ACKing all the possible interrupts so that
	 * interrupt coalescing can still be achieved.
	 */
	iwl_write32(priv, CSR_INT, priv->_agn.inta | ~priv->inta_mask);

	inta = priv->_agn.inta;

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_get_debug_level(priv) & IWL_DL_ISR) {
		/* just for debug */
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		IWL_DEBUG_ISR(priv, "inta 0x%08x, enabled 0x%08x\n ",
				inta, inta_mask);
	}
#endif

	spin_unlock_irqrestore(&priv->lock, flags);

	/* saved interrupt in inta variable now we can reset priv->_agn.inta */
	priv->_agn.inta = 0;

	/* Now service all interrupt bits discovered above. */
	if (inta & CSR_INT_BIT_HW_ERR) {
		IWL_ERR(priv, "Hardware error detected.  Restarting.\n");

		/* Tell the device to stop sending interrupts */
		iwl_disable_interrupts(priv);

		priv->isr_stats.hw++;
		iwl_irq_handle_error(priv);

		handled |= CSR_INT_BIT_HW_ERR;

		return;
	}

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_get_debug_level(priv) & (IWL_DL_ISR)) {
		/* NIC fires this, but we don't use it, redundant with WAKEUP */
		if (inta & CSR_INT_BIT_SCD) {
			IWL_DEBUG_ISR(priv, "Scheduler finished to transmit "
				      "the frame/frames.\n");
			priv->isr_stats.sch++;
		}

		/* Alive notification via Rx interrupt will do the real work */
		if (inta & CSR_INT_BIT_ALIVE) {
			IWL_DEBUG_ISR(priv, "Alive interrupt\n");
			priv->isr_stats.alive++;
		}
	}
#endif
	/* Safely ignore these bits for debug checks below */
	inta &= ~(CSR_INT_BIT_SCD | CSR_INT_BIT_ALIVE);

	/* HW RF KILL switch toggled */
	if (inta & CSR_INT_BIT_RF_KILL) {
		int hw_rf_kill = 0;
		if (!(iwl_read32(priv, CSR_GP_CNTRL) &
				CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW))
			hw_rf_kill = 1;

		IWL_WARN(priv, "RF_KILL bit toggled to %s.\n",
				hw_rf_kill ? "disable radio" : "enable radio");

		priv->isr_stats.rfkill++;

		/* driver only loads ucode once setting the interface up.
		 * the driver allows loading the ucode even if the radio
		 * is killed. Hence update the killswitch state here. The
		 * rfkill handler will care about restarting if needed.
		 */
		if (!test_bit(STATUS_ALIVE, &priv->status)) {
			if (hw_rf_kill)
				set_bit(STATUS_RF_KILL_HW, &priv->status);
			else
				clear_bit(STATUS_RF_KILL_HW, &priv->status);
			wiphy_rfkill_set_hw_state(priv->hw->wiphy, hw_rf_kill);
		}

		handled |= CSR_INT_BIT_RF_KILL;
	}

	/* Chip got too hot and stopped itself */
	if (inta & CSR_INT_BIT_CT_KILL) {
		IWL_ERR(priv, "Microcode CT kill error detected.\n");
		priv->isr_stats.ctkill++;
		handled |= CSR_INT_BIT_CT_KILL;
	}

	/* Error detected by uCode */
	if (inta & CSR_INT_BIT_SW_ERR) {
		IWL_ERR(priv, "Microcode SW error detected. "
			" Restarting 0x%X.\n", inta);
		priv->isr_stats.sw++;
		iwl_irq_handle_error(priv);
		handled |= CSR_INT_BIT_SW_ERR;
	}

	/* uCode wakes up after power-down sleep */
	if (inta & CSR_INT_BIT_WAKEUP) {
		IWL_DEBUG_ISR(priv, "Wakeup interrupt\n");
		iwl_rx_queue_update_write_ptr(priv, &priv->rxq);
		for (i = 0; i < priv->hw_params.max_txq_num; i++)
			iwl_txq_update_write_ptr(priv, &priv->txq[i]);

		priv->isr_stats.wakeup++;

		handled |= CSR_INT_BIT_WAKEUP;
	}

	/* All uCode command responses, including Tx command responses,
	 * Rx "responses" (frame-received notification), and other
	 * notifications from uCode come through here*/
	if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX |
			CSR_INT_BIT_RX_PERIODIC)) {
		IWL_DEBUG_ISR(priv, "Rx interrupt\n");
		if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX)) {
			handled |= (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX);
			iwl_write32(priv, CSR_FH_INT_STATUS,
					CSR_FH_INT_RX_MASK);
		}
		if (inta & CSR_INT_BIT_RX_PERIODIC) {
			handled |= CSR_INT_BIT_RX_PERIODIC;
			iwl_write32(priv, CSR_INT, CSR_INT_BIT_RX_PERIODIC);
		}
		/* Sending RX interrupt require many steps to be done in the
		 * the device:
		 * 1- write interrupt to current index in ICT table.
		 * 2- dma RX frame.
		 * 3- update RX shared data to indicate last write index.
		 * 4- send interrupt.
		 * This could lead to RX race, driver could receive RX interrupt
		 * but the shared data changes does not reflect this;
		 * periodic interrupt will detect any dangling Rx activity.
		 */

		/* Disable periodic interrupt; we use it as just a one-shot. */
		iwl_write8(priv, CSR_INT_PERIODIC_REG,
			    CSR_INT_PERIODIC_DIS);
		iwl_rx_handle(priv);

		/*
		 * Enable periodic interrupt in 8 msec only if we received
		 * real RX interrupt (instead of just periodic int), to catch
		 * any dangling Rx interrupt.  If it was just the periodic
		 * interrupt, there was no dangling Rx activity, and no need
		 * to extend the periodic interrupt; one-shot is enough.
		 */
		if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX))
			iwl_write8(priv, CSR_INT_PERIODIC_REG,
				    CSR_INT_PERIODIC_ENA);

		priv->isr_stats.rx++;
	}

	/* This "Tx" DMA channel is used only for loading uCode */
	if (inta & CSR_INT_BIT_FH_TX) {
		iwl_write32(priv, CSR_FH_INT_STATUS, CSR_FH_INT_TX_MASK);
		IWL_DEBUG_ISR(priv, "uCode load interrupt\n");
		priv->isr_stats.tx++;
		handled |= CSR_INT_BIT_FH_TX;
		/* Wake up uCode load routine, now that load is complete */
		priv->ucode_write_complete = 1;
		wake_up(&priv->wait_command_queue);
	}

	if (inta & ~handled) {
		IWL_ERR(priv, "Unhandled INTA bits 0x%08x\n", inta & ~handled);
		priv->isr_stats.unhandled++;
	}

	if (inta & ~(priv->inta_mask)) {
		IWL_WARN(priv, "Disabled INTA bits 0x%08x were pending\n",
			 inta & ~priv->inta_mask);
	}

	/* Re-enable all interrupts */
	/* only Re-enable if disabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl_enable_interrupts(priv);
	/* Re-enable RF_KILL if it occurred */
	else if (handled & CSR_INT_BIT_RF_KILL)
		iwl_enable_rfkill_int(priv);
}

/*****************************************************************************
 *
 * sysfs attributes
 *
 *****************************************************************************/

#ifdef CONFIG_IWLWIFI_DEBUG

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
static ssize_t show_debug_level(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	return sprintf(buf, "0x%08X\n", iwl_get_debug_level(priv));
}
static ssize_t store_debug_level(struct device *d,
				struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	unsigned long val;
	int ret;

	ret = strict_strtoul(buf, 0, &val);
	if (ret)
		IWL_ERR(priv, "%s is not in hex or decimal form.\n", buf);
	else {
		priv->debug_level = val;
		if (iwl_alloc_traffic_mem(priv))
			IWL_ERR(priv,
				"Not enough memory to generate traffic log\n");
	}
	return strnlen(buf, count);
}

static DEVICE_ATTR(debug_level, S_IWUSR | S_IRUGO,
			show_debug_level, store_debug_level);


#endif /* CONFIG_IWLWIFI_DEBUG */


static ssize_t show_temperature(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	return sprintf(buf, "%d\n", priv->temperature);
}

static DEVICE_ATTR(temperature, S_IRUGO, show_temperature, NULL);

static ssize_t show_tx_power(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	if (!iwl_is_ready_rf(priv))
		return sprintf(buf, "off\n");
	else
		return sprintf(buf, "%d\n", priv->tx_power_user_lmt);
}

static ssize_t store_tx_power(struct device *d,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	unsigned long val;
	int ret;

	ret = strict_strtoul(buf, 10, &val);
	if (ret)
		IWL_INFO(priv, "%s is not in decimal form.\n", buf);
	else {
		ret = iwl_set_tx_power(priv, val, false);
		if (ret)
			IWL_ERR(priv, "failed setting tx power (0x%d).\n",
				ret);
		else
			ret = count;
	}
	return ret;
}

static DEVICE_ATTR(tx_power, S_IWUSR | S_IRUGO, show_tx_power, store_tx_power);

static struct attribute *iwl_sysfs_entries[] = {
	&dev_attr_temperature.attr,
	&dev_attr_tx_power.attr,
#ifdef CONFIG_IWLWIFI_DEBUG
	&dev_attr_debug_level.attr,
#endif
	NULL
};

static struct attribute_group iwl_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = iwl_sysfs_entries,
};

/******************************************************************************
 *
 * uCode download functions
 *
 ******************************************************************************/

static void iwl_free_fw_desc(struct pci_dev *pci_dev, struct fw_desc *desc)
{
	if (desc->v_addr)
		dma_free_coherent(&pci_dev->dev, desc->len,
				  desc->v_addr, desc->p_addr);
	desc->v_addr = NULL;
	desc->len = 0;
}

static void iwl_free_fw_img(struct pci_dev *pci_dev, struct fw_img *img)
{
	iwl_free_fw_desc(pci_dev, &img->code);
	iwl_free_fw_desc(pci_dev, &img->data);
}

static int iwl_alloc_fw_desc(struct pci_dev *pci_dev, struct fw_desc *desc,
			     const void *data, size_t len)
{
	if (!len) {
		desc->v_addr = NULL;
		return -EINVAL;
	}

	desc->v_addr = dma_alloc_coherent(&pci_dev->dev, len,
					  &desc->p_addr, GFP_KERNEL);
	if (!desc->v_addr)
		return -ENOMEM;
	desc->len = len;
	memcpy(desc->v_addr, data, len);
	return 0;
}

static void iwl_dealloc_ucode_pci(struct iwl_priv *priv)
{
	iwl_free_fw_img(priv->pci_dev, &priv->ucode_rt);
	iwl_free_fw_img(priv->pci_dev, &priv->ucode_init);
}

struct iwlagn_ucode_capabilities {
	u32 max_probe_length;
	u32 standard_phy_calibration_size;
	u32 flags;
};

static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context);
static int iwl_mac_setup_register(struct iwl_priv *priv,
				  struct iwlagn_ucode_capabilities *capa);

#define UCODE_EXPERIMENTAL_INDEX	100
#define UCODE_EXPERIMENTAL_TAG		"exp"

static int __must_check iwl_request_firmware(struct iwl_priv *priv, bool first)
{
	const char *name_pre = priv->cfg->fw_name_pre;
	char tag[8];

	if (first) {
#ifdef CONFIG_IWLWIFI_DEBUG_EXPERIMENTAL_UCODE
		priv->fw_index = UCODE_EXPERIMENTAL_INDEX;
		strcpy(tag, UCODE_EXPERIMENTAL_TAG);
	} else if (priv->fw_index == UCODE_EXPERIMENTAL_INDEX) {
#endif
		priv->fw_index = priv->cfg->ucode_api_max;
		sprintf(tag, "%d", priv->fw_index);
	} else {
		priv->fw_index--;
		sprintf(tag, "%d", priv->fw_index);
	}

	if (priv->fw_index < priv->cfg->ucode_api_min) {
		IWL_ERR(priv, "no suitable firmware found!\n");
		return -ENOENT;
	}

	sprintf(priv->firmware_name, "%s%s%s", name_pre, tag, ".ucode");

	IWL_DEBUG_INFO(priv, "attempting to load firmware %s'%s'\n",
		       (priv->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? "EXPERIMENTAL " : "",
		       priv->firmware_name);

	return request_firmware_nowait(THIS_MODULE, 1, priv->firmware_name,
				       &priv->pci_dev->dev, GFP_KERNEL, priv,
				       iwl_ucode_callback);
}

struct iwlagn_firmware_pieces {
	const void *inst, *data, *init, *init_data;
	size_t inst_size, data_size, init_size, init_data_size;

	u32 build;

	u32 init_evtlog_ptr, init_evtlog_size, init_errlog_ptr;
	u32 inst_evtlog_ptr, inst_evtlog_size, inst_errlog_ptr;
};

static int iwlagn_load_legacy_firmware(struct iwl_priv *priv,
				       const struct firmware *ucode_raw,
				       struct iwlagn_firmware_pieces *pieces)
{
	struct iwl_ucode_header *ucode = (void *)ucode_raw->data;
	u32 api_ver, hdr_size;
	const u8 *src;

	priv->ucode_ver = le32_to_cpu(ucode->ver);
	api_ver = IWL_UCODE_API(priv->ucode_ver);

	switch (api_ver) {
	default:
		hdr_size = 28;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(priv, "File size too small!\n");
			return -EINVAL;
		}
		pieces->build = le32_to_cpu(ucode->u.v2.build);
		pieces->inst_size = le32_to_cpu(ucode->u.v2.inst_size);
		pieces->data_size = le32_to_cpu(ucode->u.v2.data_size);
		pieces->init_size = le32_to_cpu(ucode->u.v2.init_size);
		pieces->init_data_size = le32_to_cpu(ucode->u.v2.init_data_size);
		src = ucode->u.v2.data;
		break;
	case 0:
	case 1:
	case 2:
		hdr_size = 24;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(priv, "File size too small!\n");
			return -EINVAL;
		}
		pieces->build = 0;
		pieces->inst_size = le32_to_cpu(ucode->u.v1.inst_size);
		pieces->data_size = le32_to_cpu(ucode->u.v1.data_size);
		pieces->init_size = le32_to_cpu(ucode->u.v1.init_size);
		pieces->init_data_size = le32_to_cpu(ucode->u.v1.init_data_size);
		src = ucode->u.v1.data;
		break;
	}

	/* Verify size of file vs. image size info in file's header */
	if (ucode_raw->size != hdr_size + pieces->inst_size +
				pieces->data_size + pieces->init_size +
				pieces->init_data_size) {

		IWL_ERR(priv,
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

	return 0;
}

static int iwlagn_wanted_ucode_alternative = 1;

static int iwlagn_load_firmware(struct iwl_priv *priv,
				const struct firmware *ucode_raw,
				struct iwlagn_firmware_pieces *pieces,
				struct iwlagn_ucode_capabilities *capa)
{
	struct iwl_tlv_ucode_header *ucode = (void *)ucode_raw->data;
	struct iwl_ucode_tlv *tlv;
	size_t len = ucode_raw->size;
	const u8 *data;
	int wanted_alternative = iwlagn_wanted_ucode_alternative, tmp;
	u64 alternatives;
	u32 tlv_len;
	enum iwl_ucode_tlv_type tlv_type;
	const u8 *tlv_data;

	if (len < sizeof(*ucode)) {
		IWL_ERR(priv, "uCode has invalid length: %zd\n", len);
		return -EINVAL;
	}

	if (ucode->magic != cpu_to_le32(IWL_TLV_UCODE_MAGIC)) {
		IWL_ERR(priv, "invalid uCode magic: 0X%x\n",
			le32_to_cpu(ucode->magic));
		return -EINVAL;
	}

	/*
	 * Check which alternatives are present, and "downgrade"
	 * when the chosen alternative is not present, warning
	 * the user when that happens. Some files may not have
	 * any alternatives, so don't warn in that case.
	 */
	alternatives = le64_to_cpu(ucode->alternatives);
	tmp = wanted_alternative;
	if (wanted_alternative > 63)
		wanted_alternative = 63;
	while (wanted_alternative && !(alternatives & BIT(wanted_alternative)))
		wanted_alternative--;
	if (wanted_alternative && wanted_alternative != tmp)
		IWL_WARN(priv,
			 "uCode alternative %d not available, choosing %d\n",
			 tmp, wanted_alternative);

	priv->ucode_ver = le32_to_cpu(ucode->ver);
	pieces->build = le32_to_cpu(ucode->build);
	data = ucode->data;

	len -= sizeof(*ucode);

	while (len >= sizeof(*tlv)) {
		u16 tlv_alt;

		len -= sizeof(*tlv);
		tlv = (void *)data;

		tlv_len = le32_to_cpu(tlv->length);
		tlv_type = le16_to_cpu(tlv->type);
		tlv_alt = le16_to_cpu(tlv->alternative);
		tlv_data = tlv->data;

		if (len < tlv_len) {
			IWL_ERR(priv, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}
		len -= ALIGN(tlv_len, 4);
		data += sizeof(*tlv) + ALIGN(tlv_len, 4);

		/*
		 * Alternative 0 is always valid.
		 *
		 * Skip alternative TLVs that are not selected.
		 */
		if (tlv_alt != 0 && tlv_alt != wanted_alternative)
			continue;

		switch (tlv_type) {
		case IWL_UCODE_TLV_INST:
			pieces->inst = tlv_data;
			pieces->inst_size = tlv_len;
			break;
		case IWL_UCODE_TLV_DATA:
			pieces->data = tlv_data;
			pieces->data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_INIT:
			pieces->init = tlv_data;
			pieces->init_size = tlv_len;
			break;
		case IWL_UCODE_TLV_INIT_DATA:
			pieces->init_data = tlv_data;
			pieces->init_data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_BOOT:
			IWL_ERR(priv, "Found unexpected BOOT ucode\n");
			break;
		case IWL_UCODE_TLV_PROBE_MAX_LEN:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->max_probe_length =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_PAN:
			if (tlv_len)
				goto invalid_tlv_len;
			capa->flags |= IWL_UCODE_TLV_FLAGS_PAN;
			break;
		case IWL_UCODE_TLV_FLAGS:
			/* must be at least one u32 */
			if (tlv_len < sizeof(u32))
				goto invalid_tlv_len;
			/* and a proper number of u32s */
			if (tlv_len % sizeof(u32))
				goto invalid_tlv_len;
			/*
			 * This driver only reads the first u32 as
			 * right now no more features are defined,
			 * if that changes then either the driver
			 * will not work with the new firmware, or
			 * it'll not take advantage of new features.
			 */
			capa->flags = le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_errlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_errlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_ENHANCE_SENS_TBL:
			if (tlv_len)
				goto invalid_tlv_len;
			priv->enhance_sensitivity_table = true;
			break;
		case IWL_UCODE_TLV_PHY_CALIBRATION_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->standard_phy_calibration_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		default:
			IWL_DEBUG_INFO(priv, "unknown TLV: %d\n", tlv_type);
			break;
		}
	}

	if (len) {
		IWL_ERR(priv, "invalid TLV after parsing: %zd\n", len);
		iwl_print_hex_dump(priv, IWL_DL_FW, (u8 *)data, len);
		return -EINVAL;
	}

	return 0;

 invalid_tlv_len:
	IWL_ERR(priv, "TLV %d has invalid size: %u\n", tlv_type, tlv_len);
	iwl_print_hex_dump(priv, IWL_DL_FW, tlv_data, tlv_len);

	return -EINVAL;
}

/**
 * iwl_ucode_callback - callback when firmware was loaded
 *
 * If loaded successfully, copies the firmware into buffers
 * for the card to fetch (via DMA).
 */
static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context)
{
	struct iwl_priv *priv = context;
	struct iwl_ucode_header *ucode;
	int err;
	struct iwlagn_firmware_pieces pieces;
	const unsigned int api_max = priv->cfg->ucode_api_max;
	const unsigned int api_min = priv->cfg->ucode_api_min;
	u32 api_ver;
	char buildstr[25];
	u32 build;
	struct iwlagn_ucode_capabilities ucode_capa = {
		.max_probe_length = 200,
		.standard_phy_calibration_size =
			IWL_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE,
	};

	memset(&pieces, 0, sizeof(pieces));

	if (!ucode_raw) {
		if (priv->fw_index <= priv->cfg->ucode_api_max)
			IWL_ERR(priv,
				"request for firmware file '%s' failed.\n",
				priv->firmware_name);
		goto try_again;
	}

	IWL_DEBUG_INFO(priv, "Loaded firmware file '%s' (%zd bytes).\n",
		       priv->firmware_name, ucode_raw->size);

	/* Make sure that we got at least the API version number */
	if (ucode_raw->size < 4) {
		IWL_ERR(priv, "File size way too small!\n");
		goto try_again;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (struct iwl_ucode_header *)ucode_raw->data;

	if (ucode->ver)
		err = iwlagn_load_legacy_firmware(priv, ucode_raw, &pieces);
	else
		err = iwlagn_load_firmware(priv, ucode_raw, &pieces,
					   &ucode_capa);

	if (err)
		goto try_again;

	api_ver = IWL_UCODE_API(priv->ucode_ver);
	build = pieces.build;

	/*
	 * api_ver should match the api version forming part of the
	 * firmware filename ... but we don't check for that and only rely
	 * on the API version read from firmware header from here on forward
	 */
	/* no api version check required for experimental uCode */
	if (priv->fw_index != UCODE_EXPERIMENTAL_INDEX) {
		if (api_ver < api_min || api_ver > api_max) {
			IWL_ERR(priv,
				"Driver unable to support your firmware API. "
				"Driver supports v%u, firmware is v%u.\n",
				api_max, api_ver);
			goto try_again;
		}

		if (api_ver != api_max)
			IWL_ERR(priv,
				"Firmware has old API version. Expected v%u, "
				"got v%u. New firmware can be obtained "
				"from http://www.intellinuxwireless.org.\n",
				api_max, api_ver);
	}

	if (build)
		sprintf(buildstr, " build %u%s", build,
		       (priv->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? " (EXP)" : "");
	else
		buildstr[0] = '\0';

	IWL_INFO(priv, "loaded firmware version %u.%u.%u.%u%s\n",
		 IWL_UCODE_MAJOR(priv->ucode_ver),
		 IWL_UCODE_MINOR(priv->ucode_ver),
		 IWL_UCODE_API(priv->ucode_ver),
		 IWL_UCODE_SERIAL(priv->ucode_ver),
		 buildstr);

	snprintf(priv->hw->wiphy->fw_version,
		 sizeof(priv->hw->wiphy->fw_version),
		 "%u.%u.%u.%u%s",
		 IWL_UCODE_MAJOR(priv->ucode_ver),
		 IWL_UCODE_MINOR(priv->ucode_ver),
		 IWL_UCODE_API(priv->ucode_ver),
		 IWL_UCODE_SERIAL(priv->ucode_ver),
		 buildstr);

	/*
	 * For any of the failures below (before allocating pci memory)
	 * we will try to load a version with a smaller API -- maybe the
	 * user just got a corrupted version of the latest API.
	 */

	IWL_DEBUG_INFO(priv, "f/w package hdr ucode version raw = 0x%x\n",
		       priv->ucode_ver);
	IWL_DEBUG_INFO(priv, "f/w package hdr runtime inst size = %Zd\n",
		       pieces.inst_size);
	IWL_DEBUG_INFO(priv, "f/w package hdr runtime data size = %Zd\n",
		       pieces.data_size);
	IWL_DEBUG_INFO(priv, "f/w package hdr init inst size = %Zd\n",
		       pieces.init_size);
	IWL_DEBUG_INFO(priv, "f/w package hdr init data size = %Zd\n",
		       pieces.init_data_size);

	/* Verify that uCode images will fit in card's SRAM */
	if (pieces.inst_size > priv->hw_params.max_inst_size) {
		IWL_ERR(priv, "uCode instr len %Zd too large to fit in\n",
			pieces.inst_size);
		goto try_again;
	}

	if (pieces.data_size > priv->hw_params.max_data_size) {
		IWL_ERR(priv, "uCode data len %Zd too large to fit in\n",
			pieces.data_size);
		goto try_again;
	}

	if (pieces.init_size > priv->hw_params.max_inst_size) {
		IWL_ERR(priv, "uCode init instr len %Zd too large to fit in\n",
			pieces.init_size);
		goto try_again;
	}

	if (pieces.init_data_size > priv->hw_params.max_data_size) {
		IWL_ERR(priv, "uCode init data len %Zd too large to fit in\n",
			pieces.init_data_size);
		goto try_again;
	}

	/* Allocate ucode buffers for card's bus-master loading ... */

	/* Runtime instructions and 2 copies of data:
	 * 1) unmodified from disk
	 * 2) backup cache for save/restore during power-downs */
	if (iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_rt.code,
			      pieces.inst, pieces.inst_size))
		goto err_pci_alloc;
	if (iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_rt.data,
			      pieces.data, pieces.data_size))
		goto err_pci_alloc;

	/* Initialization instructions and data */
	if (pieces.init_size && pieces.init_data_size) {
		if (iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_init.code,
				      pieces.init, pieces.init_size))
			goto err_pci_alloc;
		if (iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_init.data,
				      pieces.init_data, pieces.init_data_size))
			goto err_pci_alloc;
	}

	/* Now that we can no longer fail, copy information */

	/*
	 * The (size - 16) / 12 formula is based on the information recorded
	 * for each event, which is of mode 1 (including timestamp) for all
	 * new microcodes that include this information.
	 */
	priv->_agn.init_evtlog_ptr = pieces.init_evtlog_ptr;
	if (pieces.init_evtlog_size)
		priv->_agn.init_evtlog_size = (pieces.init_evtlog_size - 16)/12;
	else
		priv->_agn.init_evtlog_size =
			priv->cfg->base_params->max_event_log_size;
	priv->_agn.init_errlog_ptr = pieces.init_errlog_ptr;
	priv->_agn.inst_evtlog_ptr = pieces.inst_evtlog_ptr;
	if (pieces.inst_evtlog_size)
		priv->_agn.inst_evtlog_size = (pieces.inst_evtlog_size - 16)/12;
	else
		priv->_agn.inst_evtlog_size =
			priv->cfg->base_params->max_event_log_size;
	priv->_agn.inst_errlog_ptr = pieces.inst_errlog_ptr;

	priv->new_scan_threshold_behaviour =
		!!(ucode_capa.flags & IWL_UCODE_TLV_FLAGS_NEWSCAN);

	if (ucode_capa.flags & IWL_UCODE_TLV_FLAGS_PAN) {
		priv->valid_contexts |= BIT(IWL_RXON_CTX_PAN);
		priv->sta_key_max_num = STA_KEY_MAX_NUM_PAN;
	} else
		priv->sta_key_max_num = STA_KEY_MAX_NUM;

	if (priv->valid_contexts != BIT(IWL_RXON_CTX_BSS))
		priv->cmd_queue = IWL_IPAN_CMD_QUEUE_NUM;
	else
		priv->cmd_queue = IWL_DEFAULT_CMD_QUEUE_NUM;

	/*
	 * figure out the offset of chain noise reset and gain commands
	 * base on the size of standard phy calibration commands table size
	 */
	if (ucode_capa.standard_phy_calibration_size >
	    IWL_MAX_PHY_CALIBRATE_TBL_SIZE)
		ucode_capa.standard_phy_calibration_size =
			IWL_MAX_STANDARD_PHY_CALIBRATE_TBL_SIZE;

	priv->_agn.phy_calib_chain_noise_reset_cmd =
		ucode_capa.standard_phy_calibration_size;
	priv->_agn.phy_calib_chain_noise_gain_cmd =
		ucode_capa.standard_phy_calibration_size + 1;

	/**************************************************
	 * This is still part of probe() in a sense...
	 *
	 * 9. Setup and register with mac80211 and debugfs
	 **************************************************/
	err = iwl_mac_setup_register(priv, &ucode_capa);
	if (err)
		goto out_unbind;

	err = iwl_dbgfs_register(priv, DRV_NAME);
	if (err)
		IWL_ERR(priv, "failed to create debugfs files. Ignoring error: %d\n", err);

	err = sysfs_create_group(&priv->pci_dev->dev.kobj,
					&iwl_attribute_group);
	if (err) {
		IWL_ERR(priv, "failed to create sysfs device attributes\n");
		goto out_unbind;
	}

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);
	complete(&priv->_agn.firmware_loading_complete);
	return;

 try_again:
	/* try next, if any */
	if (iwl_request_firmware(priv, false))
		goto out_unbind;
	release_firmware(ucode_raw);
	return;

 err_pci_alloc:
	IWL_ERR(priv, "failed to allocate pci memory\n");
	iwl_dealloc_ucode_pci(priv);
 out_unbind:
	complete(&priv->_agn.firmware_loading_complete);
	device_release_driver(&priv->pci_dev->dev);
	release_firmware(ucode_raw);
}

static const char *desc_lookup_text[] = {
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
	"NMI_INTERRUPT_BREAK_POINT"
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

static const char *desc_lookup(u32 num)
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

void iwl_dump_nic_error_log(struct iwl_priv *priv)
{
	u32 base;
	struct iwl_error_event_table table;

	base = priv->device_pointers.error_event_table;
	if (priv->ucode_type == UCODE_SUBTYPE_INIT) {
		if (!base)
			base = priv->_agn.init_errlog_ptr;
	} else {
		if (!base)
			base = priv->_agn.inst_errlog_ptr;
	}

	if (!priv->cfg->ops->lib->is_valid_rtc_data_addr(base)) {
		IWL_ERR(priv,
			"Not valid error log pointer 0x%08X for %s uCode\n",
			base,
			(priv->ucode_type == UCODE_SUBTYPE_INIT)
					? "Init" : "RT");
		return;
	}

	iwl_read_targ_mem_words(priv, base, &table, sizeof(table));

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		IWL_ERR(priv, "Start IWL Error Log Dump:\n");
		IWL_ERR(priv, "Status: 0x%08lX, count: %d\n",
			priv->status, table.valid);
	}

	priv->isr_stats.err_code = table.error_id;

	trace_iwlwifi_dev_ucode_error(priv, table.error_id, table.tsf_low,
				      table.data1, table.data2, table.line,
				      table.blink1, table.blink2, table.ilink1,
				      table.ilink2, table.bcon_time, table.gp1,
				      table.gp2, table.gp3, table.ucode_ver,
				      table.hw_ver, table.brd_ver);
	IWL_ERR(priv, "0x%08X | %-28s\n", table.error_id,
		desc_lookup(table.error_id));
	IWL_ERR(priv, "0x%08X | uPc\n", table.pc);
	IWL_ERR(priv, "0x%08X | branchlink1\n", table.blink1);
	IWL_ERR(priv, "0x%08X | branchlink2\n", table.blink2);
	IWL_ERR(priv, "0x%08X | interruptlink1\n", table.ilink1);
	IWL_ERR(priv, "0x%08X | interruptlink2\n", table.ilink2);
	IWL_ERR(priv, "0x%08X | data1\n", table.data1);
	IWL_ERR(priv, "0x%08X | data2\n", table.data2);
	IWL_ERR(priv, "0x%08X | line\n", table.line);
	IWL_ERR(priv, "0x%08X | beacon time\n", table.bcon_time);
	IWL_ERR(priv, "0x%08X | tsf low\n", table.tsf_low);
	IWL_ERR(priv, "0x%08X | tsf hi\n", table.tsf_hi);
	IWL_ERR(priv, "0x%08X | time gp1\n", table.gp1);
	IWL_ERR(priv, "0x%08X | time gp2\n", table.gp2);
	IWL_ERR(priv, "0x%08X | time gp3\n", table.gp3);
	IWL_ERR(priv, "0x%08X | uCode version\n", table.ucode_ver);
	IWL_ERR(priv, "0x%08X | hw version\n", table.hw_ver);
	IWL_ERR(priv, "0x%08X | board version\n", table.brd_ver);
	IWL_ERR(priv, "0x%08X | hcmd\n", table.hcmd);
}

#define EVENT_START_OFFSET  (4 * sizeof(u32))

/**
 * iwl_print_event_log - Dump error event log to syslog
 *
 */
static int iwl_print_event_log(struct iwl_priv *priv, u32 start_idx,
			       u32 num_events, u32 mode,
			       int pos, char **buf, size_t bufsz)
{
	u32 i;
	u32 base;       /* SRAM byte address of event log header */
	u32 event_size; /* 2 u32s, or 3 u32s if timestamp recorded */
	u32 ptr;        /* SRAM byte address of log data */
	u32 ev, time, data; /* event log data */
	unsigned long reg_flags;

	if (num_events == 0)
		return pos;

	base = priv->device_pointers.log_event_table;
	if (priv->ucode_type == UCODE_SUBTYPE_INIT) {
		if (!base)
			base = priv->_agn.init_evtlog_ptr;
	} else {
		if (!base)
			base = priv->_agn.inst_evtlog_ptr;
	}

	if (mode == 0)
		event_size = 2 * sizeof(u32);
	else
		event_size = 3 * sizeof(u32);

	ptr = base + EVENT_START_OFFSET + (start_idx * event_size);

	/* Make sure device is powered up for SRAM reads */
	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	iwl_grab_nic_access(priv);

	/* Set starting address; reads will auto-increment */
	iwl_write32(priv, HBUS_TARG_MEM_RADDR, ptr);
	rmb();

	/* "time" is actually "data" for mode 0 (no timestamp).
	* place event id # at far right for easier visual parsing. */
	for (i = 0; i < num_events; i++) {
		ev = iwl_read32(priv, HBUS_TARG_MEM_RDAT);
		time = iwl_read32(priv, HBUS_TARG_MEM_RDAT);
		if (mode == 0) {
			/* data, ev */
			if (bufsz) {
				pos += scnprintf(*buf + pos, bufsz - pos,
						"EVT_LOG:0x%08x:%04u\n",
						time, ev);
			} else {
				trace_iwlwifi_dev_ucode_event(priv, 0,
					time, ev);
				IWL_ERR(priv, "EVT_LOG:0x%08x:%04u\n",
					time, ev);
			}
		} else {
			data = iwl_read32(priv, HBUS_TARG_MEM_RDAT);
			if (bufsz) {
				pos += scnprintf(*buf + pos, bufsz - pos,
						"EVT_LOGT:%010u:0x%08x:%04u\n",
						 time, data, ev);
			} else {
				IWL_ERR(priv, "EVT_LOGT:%010u:0x%08x:%04u\n",
					time, data, ev);
				trace_iwlwifi_dev_ucode_event(priv, time,
					data, ev);
			}
		}
	}

	/* Allow device to power down */
	iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
	return pos;
}

/**
 * iwl_print_last_event_logs - Dump the newest # of event log to syslog
 */
static int iwl_print_last_event_logs(struct iwl_priv *priv, u32 capacity,
				    u32 num_wraps, u32 next_entry,
				    u32 size, u32 mode,
				    int pos, char **buf, size_t bufsz)
{
	/*
	 * display the newest DEFAULT_LOG_ENTRIES entries
	 * i.e the entries just before the next ont that uCode would fill.
	 */
	if (num_wraps) {
		if (next_entry < size) {
			pos = iwl_print_event_log(priv,
						capacity - (size - next_entry),
						size - next_entry, mode,
						pos, buf, bufsz);
			pos = iwl_print_event_log(priv, 0,
						  next_entry, mode,
						  pos, buf, bufsz);
		} else
			pos = iwl_print_event_log(priv, next_entry - size,
						  size, mode, pos, buf, bufsz);
	} else {
		if (next_entry < size) {
			pos = iwl_print_event_log(priv, 0, next_entry,
						  mode, pos, buf, bufsz);
		} else {
			pos = iwl_print_event_log(priv, next_entry - size,
						  size, mode, pos, buf, bufsz);
		}
	}
	return pos;
}

#define DEFAULT_DUMP_EVENT_LOG_ENTRIES (20)

int iwl_dump_nic_event_log(struct iwl_priv *priv, bool full_log,
			    char **buf, bool display)
{
	u32 base;       /* SRAM byte address of event log header */
	u32 capacity;   /* event log capacity in # entries */
	u32 mode;       /* 0 - no timestamp, 1 - timestamp recorded */
	u32 num_wraps;  /* # times uCode wrapped to top of log */
	u32 next_entry; /* index of next entry to be written by uCode */
	u32 size;       /* # entries that we'll print */
	u32 logsize;
	int pos = 0;
	size_t bufsz = 0;

	base = priv->device_pointers.log_event_table;
	if (priv->ucode_type == UCODE_SUBTYPE_INIT) {
		logsize = priv->_agn.init_evtlog_size;
		if (!base)
			base = priv->_agn.init_evtlog_ptr;
	} else {
		logsize = priv->_agn.inst_evtlog_size;
		if (!base)
			base = priv->_agn.inst_evtlog_ptr;
	}

	if (!priv->cfg->ops->lib->is_valid_rtc_data_addr(base)) {
		IWL_ERR(priv,
			"Invalid event log pointer 0x%08X for %s uCode\n",
			base,
			(priv->ucode_type == UCODE_SUBTYPE_INIT)
					? "Init" : "RT");
		return -EINVAL;
	}

	/* event log header */
	capacity = iwl_read_targ_mem(priv, base);
	mode = iwl_read_targ_mem(priv, base + (1 * sizeof(u32)));
	num_wraps = iwl_read_targ_mem(priv, base + (2 * sizeof(u32)));
	next_entry = iwl_read_targ_mem(priv, base + (3 * sizeof(u32)));

	if (capacity > logsize) {
		IWL_ERR(priv, "Log capacity %d is bogus, limit to %d entries\n",
			capacity, logsize);
		capacity = logsize;
	}

	if (next_entry > logsize) {
		IWL_ERR(priv, "Log write index %d is bogus, limit to %d\n",
			next_entry, logsize);
		next_entry = logsize;
	}

	size = num_wraps ? capacity : next_entry;

	/* bail out if nothing in log */
	if (size == 0) {
		IWL_ERR(priv, "Start IWL Event Log Dump: nothing in log\n");
		return pos;
	}

	/* enable/disable bt channel inhibition */
	priv->bt_ch_announce = iwlagn_bt_ch_announce;

#ifdef CONFIG_IWLWIFI_DEBUG
	if (!(iwl_get_debug_level(priv) & IWL_DL_FW_ERRORS) && !full_log)
		size = (size > DEFAULT_DUMP_EVENT_LOG_ENTRIES)
			? DEFAULT_DUMP_EVENT_LOG_ENTRIES : size;
#else
	size = (size > DEFAULT_DUMP_EVENT_LOG_ENTRIES)
		? DEFAULT_DUMP_EVENT_LOG_ENTRIES : size;
#endif
	IWL_ERR(priv, "Start IWL Event Log Dump: display last %u entries\n",
		size);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (display) {
		if (full_log)
			bufsz = capacity * 48;
		else
			bufsz = size * 48;
		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;
	}
	if ((iwl_get_debug_level(priv) & IWL_DL_FW_ERRORS) || full_log) {
		/*
		 * if uCode has wrapped back to top of log,
		 * start at the oldest entry,
		 * i.e the next one that uCode would fill.
		 */
		if (num_wraps)
			pos = iwl_print_event_log(priv, next_entry,
						capacity - next_entry, mode,
						pos, buf, bufsz);
		/* (then/else) start at top of log */
		pos = iwl_print_event_log(priv, 0,
					  next_entry, mode, pos, buf, bufsz);
	} else
		pos = iwl_print_last_event_logs(priv, capacity, num_wraps,
						next_entry, size, mode,
						pos, buf, bufsz);
#else
	pos = iwl_print_last_event_logs(priv, capacity, num_wraps,
					next_entry, size, mode,
					pos, buf, bufsz);
#endif
	return pos;
}

static void iwl_rf_kill_ct_config(struct iwl_priv *priv)
{
	struct iwl_ct_kill_config cmd;
	struct iwl_ct_kill_throttling_config adv_cmd;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&priv->lock, flags);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
	spin_unlock_irqrestore(&priv->lock, flags);
	priv->thermal_throttle.ct_kill_toggle = false;

	if (priv->cfg->base_params->support_ct_kill_exit) {
		adv_cmd.critical_temperature_enter =
			cpu_to_le32(priv->hw_params.ct_kill_threshold);
		adv_cmd.critical_temperature_exit =
			cpu_to_le32(priv->hw_params.ct_kill_exit_threshold);

		ret = iwl_send_cmd_pdu(priv, REPLY_CT_KILL_CONFIG_CMD,
				       sizeof(adv_cmd), &adv_cmd);
		if (ret)
			IWL_ERR(priv, "REPLY_CT_KILL_CONFIG_CMD failed\n");
		else
			IWL_DEBUG_INFO(priv, "REPLY_CT_KILL_CONFIG_CMD "
					"succeeded, "
					"critical temperature enter is %d,"
					"exit is %d\n",
				       priv->hw_params.ct_kill_threshold,
				       priv->hw_params.ct_kill_exit_threshold);
	} else {
		cmd.critical_temperature_R =
			cpu_to_le32(priv->hw_params.ct_kill_threshold);

		ret = iwl_send_cmd_pdu(priv, REPLY_CT_KILL_CONFIG_CMD,
				       sizeof(cmd), &cmd);
		if (ret)
			IWL_ERR(priv, "REPLY_CT_KILL_CONFIG_CMD failed\n");
		else
			IWL_DEBUG_INFO(priv, "REPLY_CT_KILL_CONFIG_CMD "
					"succeeded, "
					"critical temperature is %d\n",
					priv->hw_params.ct_kill_threshold);
	}
}

static int iwlagn_send_calib_cfg_rt(struct iwl_priv *priv, u32 cfg)
{
	struct iwl_calib_cfg_cmd calib_cfg_cmd;
	struct iwl_host_cmd cmd = {
		.id = CALIBRATION_CFG_CMD,
		.len = { sizeof(struct iwl_calib_cfg_cmd), },
		.data = { &calib_cfg_cmd, },
	};

	memset(&calib_cfg_cmd, 0, sizeof(calib_cfg_cmd));
	calib_cfg_cmd.ucd_calib_cfg.once.is_enable = IWL_CALIB_INIT_CFG_ALL;
	calib_cfg_cmd.ucd_calib_cfg.once.start = cpu_to_le32(cfg);

	return iwl_send_cmd(priv, &cmd);
}


/**
 * iwl_alive_start - called after REPLY_ALIVE notification received
 *                   from protocol/runtime uCode (initialization uCode's
 *                   Alive gets handled by iwl_init_alive_start()).
 */
int iwl_alive_start(struct iwl_priv *priv)
{
	int ret = 0;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	iwl_reset_ict(priv);

	IWL_DEBUG_INFO(priv, "Runtime Alive received.\n");

	/* After the ALIVE response, we can send host commands to the uCode */
	set_bit(STATUS_ALIVE, &priv->status);

	/* Enable watchdog to monitor the driver tx queues */
	iwl_setup_watchdog(priv);

	if (iwl_is_rfkill(priv))
		return -ERFKILL;

	/* download priority table before any calibration request */
	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist) {
		/* Configure Bluetooth device coexistence support */
		priv->bt_valid = IWLAGN_BT_ALL_VALID_MSK;
		priv->kill_ack_mask = IWLAGN_BT_KILL_ACK_MASK_DEFAULT;
		priv->kill_cts_mask = IWLAGN_BT_KILL_CTS_MASK_DEFAULT;
		priv->cfg->ops->hcmd->send_bt_config(priv);
		priv->bt_valid = IWLAGN_BT_VALID_ENABLE_FLAGS;
		iwlagn_send_prio_tbl(priv);

		/* FIXME: w/a to force change uCode BT state machine */
		ret = iwlagn_send_bt_env(priv, IWL_BT_COEX_ENV_OPEN,
					 BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
		if (ret)
			return ret;
		ret = iwlagn_send_bt_env(priv, IWL_BT_COEX_ENV_CLOSE,
					 BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
		if (ret)
			return ret;
	}
	if (priv->hw_params.calib_rt_cfg)
		iwlagn_send_calib_cfg_rt(priv, priv->hw_params.calib_rt_cfg);

	ieee80211_wake_queues(priv->hw);

	priv->active_rate = IWL_RATES_MASK;

	/* Configure Tx antenna selection based on H/W config */
	if (priv->cfg->ops->hcmd->set_tx_ant)
		priv->cfg->ops->hcmd->set_tx_ant(priv, priv->cfg->valid_tx_ant);

	if (iwl_is_associated_ctx(ctx)) {
		struct iwl_rxon_cmd *active_rxon =
				(struct iwl_rxon_cmd *)&ctx->active;
		/* apply any changes in staging */
		ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	} else {
		struct iwl_rxon_context *tmp;
		/* Initialize our rx_config data */
		for_each_context(priv, tmp)
			iwl_connection_init_rx_config(priv, tmp);

		if (priv->cfg->ops->hcmd->set_rxon_chain)
			priv->cfg->ops->hcmd->set_rxon_chain(priv, ctx);
	}

	if (!priv->cfg->bt_params || (priv->cfg->bt_params &&
	    !priv->cfg->bt_params->advanced_bt_coexist)) {
		/*
		 * default is 2-wire BT coexexistence support
		 */
		priv->cfg->ops->hcmd->send_bt_config(priv);
	}

	iwl_reset_run_time_calib(priv);

	set_bit(STATUS_READY, &priv->status);

	/* Configure the adapter for unassociated operation */
	ret = iwlcore_commit_rxon(priv, ctx);
	if (ret)
		return ret;

	/* At this point, the NIC is initialized and operational */
	iwl_rf_kill_ct_config(priv);

	IWL_DEBUG_INFO(priv, "ALIVE processing complete.\n");

	return iwl_power_update_mode(priv, true);
}

static void iwl_cancel_deferred_work(struct iwl_priv *priv);

static void __iwl_down(struct iwl_priv *priv)
{
	int exit_pending;

	IWL_DEBUG_INFO(priv, DRV_NAME " is going down\n");

	iwl_scan_cancel_timeout(priv, 200);

	exit_pending = test_and_set_bit(STATUS_EXIT_PENDING, &priv->status);

	/* Stop TX queues watchdog. We need to have STATUS_EXIT_PENDING bit set
	 * to prevent rearm timer */
	del_timer_sync(&priv->watchdog);

	iwl_clear_ucode_stations(priv, NULL);
	iwl_dealloc_bcast_stations(priv);
	iwl_clear_driver_stations(priv);

	/* reset BT coex data */
	priv->bt_status = 0;
	if (priv->cfg->bt_params)
		priv->bt_traffic_load =
			 priv->cfg->bt_params->bt_init_traffic_load;
	else
		priv->bt_traffic_load = 0;
	priv->bt_full_concurrent = false;
	priv->bt_ci_compliance = 0;

	/* Wipe out the EXIT_PENDING status bit if we are not actually
	 * exiting the module */
	if (!exit_pending)
		clear_bit(STATUS_EXIT_PENDING, &priv->status);

	if (priv->mac80211_registered)
		ieee80211_stop_queues(priv->hw);

	/* Clear out all status bits but a few that are stable across reset */
	priv->status &= test_bit(STATUS_RF_KILL_HW, &priv->status) <<
				STATUS_RF_KILL_HW |
			test_bit(STATUS_GEO_CONFIGURED, &priv->status) <<
				STATUS_GEO_CONFIGURED |
			test_bit(STATUS_FW_ERROR, &priv->status) <<
				STATUS_FW_ERROR |
		       test_bit(STATUS_EXIT_PENDING, &priv->status) <<
				STATUS_EXIT_PENDING;

	iwlagn_stop_device(priv);

	dev_kfree_skb(priv->beacon_skb);
	priv->beacon_skb = NULL;
}

static void iwl_down(struct iwl_priv *priv)
{
	mutex_lock(&priv->mutex);
	__iwl_down(priv);
	mutex_unlock(&priv->mutex);

	iwl_cancel_deferred_work(priv);
}

#define HW_READY_TIMEOUT (50)

/* Note: returns poll_bit return value, which is >= 0 if success */
static int iwl_set_hw_ready(struct iwl_priv *priv)
{
	int ret;

	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
		CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	/* See if we got it */
	ret = iwl_poll_bit(priv, CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
				CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
				HW_READY_TIMEOUT);

	IWL_DEBUG_INFO(priv, "hardware%s ready\n", ret < 0 ? " not" : "");
	return ret;
}

/* Note: returns standard 0/-ERROR code */
int iwl_prepare_card_hw(struct iwl_priv *priv)
{
	int ret;

	IWL_DEBUG_INFO(priv, "iwl_prepare_card_hw enter\n");

	ret = iwl_set_hw_ready(priv);
	if (ret >= 0)
		return 0;

	/* If HW is not ready, prepare the conditions to check again */
	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			CSR_HW_IF_CONFIG_REG_PREPARE);

	ret = iwl_poll_bit(priv, CSR_HW_IF_CONFIG_REG,
			~CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE,
			CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE, 150000);

	if (ret < 0)
		return ret;

	/* HW should be ready by now, check again. */
	ret = iwl_set_hw_ready(priv);
	if (ret >= 0)
		return 0;
	return ret;
}

#define MAX_HW_RESTARTS 5

static int __iwl_up(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;
	int ret;

	lockdep_assert_held(&priv->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_WARN(priv, "Exit pending; will not bring the NIC up\n");
		return -EIO;
	}

	for_each_context(priv, ctx) {
		ret = iwlagn_alloc_bcast_station(priv, ctx);
		if (ret) {
			iwl_dealloc_bcast_stations(priv);
			return ret;
		}
	}

	ret = iwlagn_run_init_ucode(priv);
	if (ret) {
		IWL_ERR(priv, "Failed to run INIT ucode: %d\n", ret);
		goto error;
	}

	ret = iwlagn_load_ucode_wait_alive(priv,
					   &priv->ucode_rt,
					   UCODE_SUBTYPE_REGULAR,
					   UCODE_SUBTYPE_REGULAR_NEW);
	if (ret) {
		IWL_ERR(priv, "Failed to start RT ucode: %d\n", ret);
		goto error;
	}

	ret = iwl_alive_start(priv);
	if (ret)
		goto error;
	return 0;

 error:
	set_bit(STATUS_EXIT_PENDING, &priv->status);
	__iwl_down(priv);
	clear_bit(STATUS_EXIT_PENDING, &priv->status);

	IWL_ERR(priv, "Unable to initialize device.\n");
	return ret;
}


/*****************************************************************************
 *
 * Workqueue callbacks
 *
 *****************************************************************************/

static void iwl_bg_run_time_calib_work(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv,
			run_time_calib_work);

	mutex_lock(&priv->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status) ||
	    test_bit(STATUS_SCANNING, &priv->status)) {
		mutex_unlock(&priv->mutex);
		return;
	}

	if (priv->start_calib) {
		iwl_chain_noise_calibration(priv);
		iwl_sensitivity_calibration(priv);
	}

	mutex_unlock(&priv->mutex);
}

static void iwlagn_prepare_restart(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;
	bool bt_full_concurrent;
	u8 bt_ci_compliance;
	u8 bt_load;
	u8 bt_status;

	lockdep_assert_held(&priv->mutex);

	for_each_context(priv, ctx)
		ctx->vif = NULL;
	priv->is_open = 0;

	/*
	 * __iwl_down() will clear the BT status variables,
	 * which is correct, but when we restart we really
	 * want to keep them so restore them afterwards.
	 *
	 * The restart process will later pick them up and
	 * re-configure the hw when we reconfigure the BT
	 * command.
	 */
	bt_full_concurrent = priv->bt_full_concurrent;
	bt_ci_compliance = priv->bt_ci_compliance;
	bt_load = priv->bt_traffic_load;
	bt_status = priv->bt_status;

	__iwl_down(priv);

	priv->bt_full_concurrent = bt_full_concurrent;
	priv->bt_ci_compliance = bt_ci_compliance;
	priv->bt_traffic_load = bt_load;
	priv->bt_status = bt_status;
}

static void iwl_bg_restart(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, restart);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (test_and_clear_bit(STATUS_FW_ERROR, &priv->status)) {
		mutex_lock(&priv->mutex);
		iwlagn_prepare_restart(priv);
		mutex_unlock(&priv->mutex);
		iwl_cancel_deferred_work(priv);
		ieee80211_restart_hw(priv->hw);
	} else {
		WARN_ON(1);
	}
}

static void iwl_bg_rx_replenish(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, rx_replenish);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwlagn_rx_replenish(priv);
	mutex_unlock(&priv->mutex);
}

static int iwl_mac_offchannel_tx(struct ieee80211_hw *hw, struct sk_buff *skb,
				 struct ieee80211_channel *chan,
				 enum nl80211_channel_type channel_type,
				 unsigned int wait)
{
	struct iwl_priv *priv = hw->priv;
	int ret;

	/* Not supported if we don't have PAN */
	if (!(priv->valid_contexts & BIT(IWL_RXON_CTX_PAN))) {
		ret = -EOPNOTSUPP;
		goto free;
	}

	/* Not supported on pre-P2P firmware */
	if (!(priv->contexts[IWL_RXON_CTX_PAN].interface_modes &
					BIT(NL80211_IFTYPE_P2P_CLIENT))) {
		ret = -EOPNOTSUPP;
		goto free;
	}

	mutex_lock(&priv->mutex);

	if (!priv->contexts[IWL_RXON_CTX_PAN].is_active) {
		/*
		 * If the PAN context is free, use the normal
		 * way of doing remain-on-channel offload + TX.
		 */
		ret = 1;
		goto out;
	}

	/* TODO: queue up if scanning? */
	if (test_bit(STATUS_SCANNING, &priv->status) ||
	    priv->_agn.offchan_tx_skb) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * max_scan_ie_len doesn't include the blank SSID or the header,
	 * so need to add that again here.
	 */
	if (skb->len > hw->wiphy->max_scan_ie_len + 24 + 2) {
		ret = -ENOBUFS;
		goto out;
	}

	priv->_agn.offchan_tx_skb = skb;
	priv->_agn.offchan_tx_timeout = wait;
	priv->_agn.offchan_tx_chan = chan;

	ret = iwl_scan_initiate(priv, priv->contexts[IWL_RXON_CTX_PAN].vif,
				IWL_SCAN_OFFCH_TX, chan->band);
	if (ret)
		priv->_agn.offchan_tx_skb = NULL;
 out:
	mutex_unlock(&priv->mutex);
 free:
	if (ret < 0)
		kfree_skb(skb);

	return ret;
}

static int iwl_mac_offchannel_tx_cancel_wait(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	int ret;

	mutex_lock(&priv->mutex);

	if (!priv->_agn.offchan_tx_skb) {
		ret = -EINVAL;
		goto unlock;
	}

	priv->_agn.offchan_tx_skb = NULL;

	ret = iwl_scan_cancel_timeout(priv, 200);
	if (ret)
		ret = -EIO;
unlock:
	mutex_unlock(&priv->mutex);

	return ret;
}

/*****************************************************************************
 *
 * mac80211 entry point functions
 *
 *****************************************************************************/

/*
 * Not a mac80211 entry point function, but it fits in with all the
 * other mac80211 functions grouped here.
 */
static int iwl_mac_setup_register(struct iwl_priv *priv,
				  struct iwlagn_ucode_capabilities *capa)
{
	int ret;
	struct ieee80211_hw *hw = priv->hw;
	struct iwl_rxon_context *ctx;

	hw->rate_control_algorithm = "iwl-agn-rs";

	/* Tell mac80211 our characteristics */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_AMPDU_AGGREGATION |
		    IEEE80211_HW_NEED_DTIM_PERIOD |
		    IEEE80211_HW_SPECTRUM_MGMT |
		    IEEE80211_HW_REPORTS_TX_ACK_STATUS;

	/*
	 * Including the following line will crash some AP's.  This
	 * workaround removes the stimulus which causes the crash until
	 * the AP software can be fixed.
	hw->max_tx_aggregation_subframes = LINK_QUAL_AGG_FRAME_LIMIT_DEF;
	 */

	hw->flags |= IEEE80211_HW_SUPPORTS_PS |
		     IEEE80211_HW_SUPPORTS_DYNAMIC_PS;

	if (priv->cfg->sku & IWL_SKU_N)
		hw->flags |= IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS |
			     IEEE80211_HW_SUPPORTS_STATIC_SMPS;

	if (capa->flags & IWL_UCODE_TLV_FLAGS_MFP)
		hw->flags |= IEEE80211_HW_MFP_CAPABLE;

	hw->sta_data_size = sizeof(struct iwl_station_priv);
	hw->vif_data_size = sizeof(struct iwl_vif_priv);

	for_each_context(priv, ctx) {
		hw->wiphy->interface_modes |= ctx->interface_modes;
		hw->wiphy->interface_modes |= ctx->exclusive_interface_modes;
	}

	hw->wiphy->max_remain_on_channel_duration = 1000;

	hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY |
			    WIPHY_FLAG_DISABLE_BEACON_HINTS |
			    WIPHY_FLAG_IBSS_RSN;

	/*
	 * For now, disable PS by default because it affects
	 * RX performance significantly.
	 */
	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->max_scan_ssids = PROBE_OPTION_MAX;
	/* we create the 802.11 header and a zero-length SSID element */
	hw->wiphy->max_scan_ie_len = capa->max_probe_length - 24 - 2;

	/* Default value; 4 EDCA QOS priorities */
	hw->queues = 4;

	hw->max_listen_interval = IWL_CONN_MAX_LISTEN_INTERVAL;

	if (priv->bands[IEEE80211_BAND_2GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&priv->bands[IEEE80211_BAND_2GHZ];
	if (priv->bands[IEEE80211_BAND_5GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&priv->bands[IEEE80211_BAND_5GHZ];

	iwl_leds_init(priv);

	ret = ieee80211_register_hw(priv->hw);
	if (ret) {
		IWL_ERR(priv, "Failed to register hw (error %d)\n", ret);
		return ret;
	}
	priv->mac80211_registered = 1;

	return 0;
}


static int iwlagn_mac_start(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	int ret;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	/* we should be verifying the device is ready to be opened */
	mutex_lock(&priv->mutex);
	ret = __iwl_up(priv);
	mutex_unlock(&priv->mutex);
	if (ret)
		return ret;

	IWL_DEBUG_INFO(priv, "Start UP work done.\n");

	/* Now we should be done, and the READY bit should be set. */
	if (WARN_ON(!test_bit(STATUS_READY, &priv->status)))
		ret = -EIO;

	iwlagn_led_enable(priv);

	priv->is_open = 1;
	IWL_DEBUG_MAC80211(priv, "leave\n");
	return 0;
}

static void iwlagn_mac_stop(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!priv->is_open)
		return;

	priv->is_open = 0;

	iwl_down(priv);

	flush_workqueue(priv->workqueue);

	/* User space software may expect getting rfkill changes
	 * even if interface is down */
	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);
	iwl_enable_rfkill_int(priv);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static void iwlagn_mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MACDUMP(priv, "enter\n");

	IWL_DEBUG_TX(priv, "dev->xmit(%d bytes) at rate 0x%02x\n", skb->len,
		     ieee80211_get_tx_rate(hw, IEEE80211_SKB_CB(skb))->bitrate);

	if (iwlagn_tx_skb(priv, skb))
		dev_kfree_skb_any(skb);

	IWL_DEBUG_MACDUMP(priv, "leave\n");
}

static void iwlagn_mac_update_tkip_key(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_key_conf *keyconf,
				       struct ieee80211_sta *sta,
				       u32 iv32, u16 *phase1key)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	iwl_update_tkip_key(priv, vif_priv->ctx, keyconf, sta,
			    iv32, phase1key);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static int iwlagn_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct ieee80211_key_conf *key)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;
	struct iwl_rxon_context *ctx = vif_priv->ctx;
	int ret;
	u8 sta_id;
	bool is_default_wep_key = false;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (iwlagn_mod_params.sw_crypto) {
		IWL_DEBUG_MAC80211(priv, "leave - hwcrypto disabled\n");
		return -EOPNOTSUPP;
	}

	/*
	 * To support IBSS RSN, don't program group keys in IBSS, the
	 * hardware will then not attempt to decrypt the frames.
	 */
	if (vif->type == NL80211_IFTYPE_ADHOC &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	sta_id = iwl_sta_id_or_broadcast(priv, vif_priv->ctx, sta);
	if (sta_id == IWL_INVALID_STATION)
		return -EINVAL;

	mutex_lock(&priv->mutex);
	iwl_scan_cancel_timeout(priv, 100);

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
			ret = iwl_set_default_wep_key(priv, vif_priv->ctx, key);
		else
			ret = iwl_set_dynamic_key(priv, vif_priv->ctx,
						  key, sta_id);

		IWL_DEBUG_MAC80211(priv, "enable hwcrypto key\n");
		break;
	case DISABLE_KEY:
		if (is_default_wep_key)
			ret = iwl_remove_default_wep_key(priv, ctx, key);
		else
			ret = iwl_remove_dynamic_key(priv, ctx, key, sta_id);

		IWL_DEBUG_MAC80211(priv, "disable hwcrypto key\n");
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&priv->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");

	return ret;
}

static int iwlagn_mac_ampdu_action(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   enum ieee80211_ampdu_mlme_action action,
				   struct ieee80211_sta *sta, u16 tid, u16 *ssn,
				   u8 buf_size)
{
	struct iwl_priv *priv = hw->priv;
	int ret = -EINVAL;
	struct iwl_station_priv *sta_priv = (void *) sta->drv_priv;

	IWL_DEBUG_HT(priv, "A-MPDU action on addr %pM tid %d\n",
		     sta->addr, tid);

	if (!(priv->cfg->sku & IWL_SKU_N))
		return -EACCES;

	mutex_lock(&priv->mutex);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		IWL_DEBUG_HT(priv, "start Rx\n");
		ret = iwl_sta_rx_agg_start(priv, sta, tid, *ssn);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		IWL_DEBUG_HT(priv, "stop Rx\n");
		ret = iwl_sta_rx_agg_stop(priv, sta, tid);
		if (test_bit(STATUS_EXIT_PENDING, &priv->status))
			ret = 0;
		break;
	case IEEE80211_AMPDU_TX_START:
		IWL_DEBUG_HT(priv, "start Tx\n");
		ret = iwlagn_tx_agg_start(priv, vif, sta, tid, ssn);
		if (ret == 0) {
			priv->_agn.agg_tids_count++;
			IWL_DEBUG_HT(priv, "priv->_agn.agg_tids_count = %u\n",
				     priv->_agn.agg_tids_count);
		}
		break;
	case IEEE80211_AMPDU_TX_STOP:
		IWL_DEBUG_HT(priv, "stop Tx\n");
		ret = iwlagn_tx_agg_stop(priv, vif, sta, tid);
		if ((ret == 0) && (priv->_agn.agg_tids_count > 0)) {
			priv->_agn.agg_tids_count--;
			IWL_DEBUG_HT(priv, "priv->_agn.agg_tids_count = %u\n",
				     priv->_agn.agg_tids_count);
		}
		if (test_bit(STATUS_EXIT_PENDING, &priv->status))
			ret = 0;
		if (priv->cfg->ht_params &&
		    priv->cfg->ht_params->use_rts_for_aggregation) {
			struct iwl_station_priv *sta_priv =
				(void *) sta->drv_priv;
			/*
			 * switch off RTS/CTS if it was previously enabled
			 */

			sta_priv->lq_sta.lq.general_params.flags &=
				~LINK_QUAL_FLAGS_SET_STA_TLC_RTS_MSK;
			iwl_send_lq_cmd(priv, iwl_rxon_ctx_from_vif(vif),
					&sta_priv->lq_sta.lq, CMD_ASYNC, false);
		}
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		buf_size = min_t(int, buf_size, LINK_QUAL_AGG_FRAME_LIMIT_DEF);

		iwlagn_txq_agg_queue_setup(priv, sta, tid, buf_size);

		/*
		 * If the limit is 0, then it wasn't initialised yet,
		 * use the default. We can do that since we take the
		 * minimum below, and we don't want to go above our
		 * default due to hardware restrictions.
		 */
		if (sta_priv->max_agg_bufsize == 0)
			sta_priv->max_agg_bufsize =
				LINK_QUAL_AGG_FRAME_LIMIT_DEF;

		/*
		 * Even though in theory the peer could have different
		 * aggregation reorder buffer sizes for different sessions,
		 * our ucode doesn't allow for that and has a global limit
		 * for each station. Therefore, use the minimum of all the
		 * aggregation sessions and our default value.
		 */
		sta_priv->max_agg_bufsize =
			min(sta_priv->max_agg_bufsize, buf_size);

		if (priv->cfg->ht_params &&
		    priv->cfg->ht_params->use_rts_for_aggregation) {
			/*
			 * switch to RTS/CTS if it is the prefer protection
			 * method for HT traffic
			 */

			sta_priv->lq_sta.lq.general_params.flags |=
				LINK_QUAL_FLAGS_SET_STA_TLC_RTS_MSK;
		}

		sta_priv->lq_sta.lq.agg_params.agg_frame_cnt_limit =
			sta_priv->max_agg_bufsize;

		iwl_send_lq_cmd(priv, iwl_rxon_ctx_from_vif(vif),
				&sta_priv->lq_sta.lq, CMD_ASYNC, false);
		ret = 0;
		break;
	}
	mutex_unlock(&priv->mutex);

	return ret;
}

static int iwlagn_mac_sta_add(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_station_priv *sta_priv = (void *)sta->drv_priv;
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;
	bool is_ap = vif->type == NL80211_IFTYPE_STATION;
	int ret;
	u8 sta_id;

	IWL_DEBUG_INFO(priv, "received request to add station %pM\n",
			sta->addr);
	mutex_lock(&priv->mutex);
	IWL_DEBUG_INFO(priv, "proceeding to add station %pM\n",
			sta->addr);
	sta_priv->common.sta_id = IWL_INVALID_STATION;

	atomic_set(&sta_priv->pending_frames, 0);
	if (vif->type == NL80211_IFTYPE_AP)
		sta_priv->client = true;

	ret = iwl_add_station_common(priv, vif_priv->ctx, sta->addr,
				     is_ap, sta, &sta_id);
	if (ret) {
		IWL_ERR(priv, "Unable to add station %pM (%d)\n",
			sta->addr, ret);
		/* Should we return success if return code is EEXIST ? */
		mutex_unlock(&priv->mutex);
		return ret;
	}

	sta_priv->common.sta_id = sta_id;

	/* Initialize rate scaling */
	IWL_DEBUG_INFO(priv, "Initializing rate scaling for station %pM\n",
		       sta->addr);
	iwl_rs_rate_init(priv, sta, sta_id);
	mutex_unlock(&priv->mutex);

	return 0;
}

static void iwlagn_mac_channel_switch(struct ieee80211_hw *hw,
				struct ieee80211_channel_switch *ch_switch)
{
	struct iwl_priv *priv = hw->priv;
	const struct iwl_channel_info *ch_info;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_channel *channel = ch_switch->channel;
	struct iwl_ht_config *ht_conf = &priv->current_ht_config;
	/*
	 * MULTI-FIXME
	 * When we add support for multiple interfaces, we need to
	 * revisit this. The channel switch command in the device
	 * only affects the BSS context, but what does that really
	 * mean? And what if we get a CSA on the second interface?
	 * This needs a lot of work.
	 */
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	u16 ch;
	unsigned long flags = 0;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	mutex_lock(&priv->mutex);

	if (iwl_is_rfkill(priv))
		goto out;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status) ||
	    test_bit(STATUS_SCANNING, &priv->status) ||
	    test_bit(STATUS_CHANNEL_SWITCH_PENDING, &priv->status))
		goto out;

	if (!iwl_is_associated_ctx(ctx))
		goto out;

	if (priv->cfg->ops->lib->set_channel_switch) {

		ch = channel->hw_value;
		if (le16_to_cpu(ctx->active.channel) != ch) {
			ch_info = iwl_get_channel_info(priv,
						       channel->band,
						       ch);
			if (!is_channel_valid(ch_info)) {
				IWL_DEBUG_MAC80211(priv, "invalid channel\n");
				goto out;
			}
			spin_lock_irqsave(&priv->lock, flags);

			priv->current_ht_config.smps = conf->smps_mode;

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

			iwl_set_rxon_channel(priv, channel, ctx);
			iwl_set_rxon_ht(priv, ht_conf);
			iwl_set_flags_for_band(priv, ctx, channel->band,
					       ctx->vif);
			spin_unlock_irqrestore(&priv->lock, flags);

			iwl_set_rate(priv);
			/*
			 * at this point, staging_rxon has the
			 * configuration for channel switch
			 */
			set_bit(STATUS_CHANNEL_SWITCH_PENDING, &priv->status);
			priv->switch_channel = cpu_to_le16(ch);
			if (priv->cfg->ops->lib->set_channel_switch(priv,
								    ch_switch)) {
				clear_bit(STATUS_CHANNEL_SWITCH_PENDING,
					  &priv->status);
				priv->switch_channel = 0;
				ieee80211_chswitch_done(ctx->vif, false);
			}
		}
	}
out:
	mutex_unlock(&priv->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static void iwlagn_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	struct iwl_priv *priv = hw->priv;
	__le32 filter_or = 0, filter_nand = 0;
	struct iwl_rxon_context *ctx;

#define CHK(test, flag)	do { \
	if (*total_flags & (test))		\
		filter_or |= (flag);		\
	else					\
		filter_nand |= (flag);		\
	} while (0)

	IWL_DEBUG_MAC80211(priv, "Enter: changed: 0x%x, total: 0x%x\n",
			changed_flags, *total_flags);

	CHK(FIF_OTHER_BSS | FIF_PROMISC_IN_BSS, RXON_FILTER_PROMISC_MSK);
	/* Setting _just_ RXON_FILTER_CTL2HOST_MSK causes FH errors */
	CHK(FIF_CONTROL, RXON_FILTER_CTL2HOST_MSK | RXON_FILTER_PROMISC_MSK);
	CHK(FIF_BCN_PRBRESP_PROMISC, RXON_FILTER_BCON_AWARE_MSK);

#undef CHK

	mutex_lock(&priv->mutex);

	for_each_context(priv, ctx) {
		ctx->staging.filter_flags &= ~filter_nand;
		ctx->staging.filter_flags |= filter_or;

		/*
		 * Not committing directly because hardware can perform a scan,
		 * but we'll eventually commit the filter flags change anyway.
		 */
	}

	mutex_unlock(&priv->mutex);

	/*
	 * Receiving all multicast frames is always enabled by the
	 * default flags setup in iwl_connection_init_rx_config()
	 * since we currently do not support programming multicast
	 * filters into the device.
	 */
	*total_flags &= FIF_OTHER_BSS | FIF_ALLMULTI | FIF_PROMISC_IN_BSS |
			FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL;
}

static void iwlagn_mac_flush(struct ieee80211_hw *hw, bool drop)
{
	struct iwl_priv *priv = hw->priv;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211(priv, "enter\n");

	/* do not support "flush" */
	if (!priv->cfg->ops->lib->txfifo_flush)
		goto done;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_DEBUG_TX(priv, "Aborting flush due to device shutdown\n");
		goto done;
	}
	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_TX(priv, "Aborting flush due to RF Kill\n");
		goto done;
	}

	/*
	 * mac80211 will not push any more frames for transmit
	 * until the flush is completed
	 */
	if (drop) {
		IWL_DEBUG_MAC80211(priv, "send flush command\n");
		if (priv->cfg->ops->lib->txfifo_flush(priv, IWL_DROP_ALL)) {
			IWL_ERR(priv, "flush request fail\n");
			goto done;
		}
	}
	IWL_DEBUG_MAC80211(priv, "wait transmit/flush all frames\n");
	iwlagn_wait_tx_queue_empty(priv);
done:
	mutex_unlock(&priv->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static void iwlagn_disable_roc(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_PAN];
	struct ieee80211_channel *chan = ACCESS_ONCE(priv->hw->conf.channel);

	lockdep_assert_held(&priv->mutex);

	if (!ctx->is_active)
		return;

	ctx->staging.dev_type = RXON_DEV_TYPE_2STA;
	ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwl_set_rxon_channel(priv, chan, ctx);
	iwl_set_flags_for_band(priv, ctx, chan->band, NULL);

	priv->_agn.hw_roc_channel = NULL;

	iwlcore_commit_rxon(priv, ctx);

	ctx->is_active = false;
}

static void iwlagn_bg_roc_done(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv,
					     _agn.hw_roc_work.work);

	mutex_lock(&priv->mutex);
	ieee80211_remain_on_channel_expired(priv->hw);
	iwlagn_disable_roc(priv);
	mutex_unlock(&priv->mutex);
}

static int iwl_mac_remain_on_channel(struct ieee80211_hw *hw,
				     struct ieee80211_channel *channel,
				     enum nl80211_channel_type channel_type,
				     int duration)
{
	struct iwl_priv *priv = hw->priv;
	int err = 0;

	if (!(priv->valid_contexts & BIT(IWL_RXON_CTX_PAN)))
		return -EOPNOTSUPP;

	if (!(priv->contexts[IWL_RXON_CTX_PAN].interface_modes &
					BIT(NL80211_IFTYPE_P2P_CLIENT)))
		return -EOPNOTSUPP;

	mutex_lock(&priv->mutex);

	if (priv->contexts[IWL_RXON_CTX_PAN].is_active ||
	    test_bit(STATUS_SCAN_HW, &priv->status)) {
		err = -EBUSY;
		goto out;
	}

	priv->contexts[IWL_RXON_CTX_PAN].is_active = true;
	priv->_agn.hw_roc_channel = channel;
	priv->_agn.hw_roc_chantype = channel_type;
	priv->_agn.hw_roc_duration = DIV_ROUND_UP(duration * 1000, 1024);
	iwlcore_commit_rxon(priv, &priv->contexts[IWL_RXON_CTX_PAN]);
	queue_delayed_work(priv->workqueue, &priv->_agn.hw_roc_work,
			   msecs_to_jiffies(duration + 20));

	msleep(IWL_MIN_SLOT_TIME); /* TU is almost ms */
	ieee80211_ready_on_channel(priv->hw);

 out:
	mutex_unlock(&priv->mutex);

	return err;
}

static int iwl_mac_cancel_remain_on_channel(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;

	if (!(priv->valid_contexts & BIT(IWL_RXON_CTX_PAN)))
		return -EOPNOTSUPP;

	cancel_delayed_work_sync(&priv->_agn.hw_roc_work);

	mutex_lock(&priv->mutex);
	iwlagn_disable_roc(priv);
	mutex_unlock(&priv->mutex);

	return 0;
}

/*****************************************************************************
 *
 * driver setup and teardown
 *
 *****************************************************************************/

static void iwl_setup_deferred_work(struct iwl_priv *priv)
{
	priv->workqueue = create_singlethread_workqueue(DRV_NAME);

	init_waitqueue_head(&priv->wait_command_queue);

	INIT_WORK(&priv->restart, iwl_bg_restart);
	INIT_WORK(&priv->rx_replenish, iwl_bg_rx_replenish);
	INIT_WORK(&priv->beacon_update, iwl_bg_beacon_update);
	INIT_WORK(&priv->run_time_calib_work, iwl_bg_run_time_calib_work);
	INIT_WORK(&priv->tx_flush, iwl_bg_tx_flush);
	INIT_WORK(&priv->bt_full_concurrency, iwl_bg_bt_full_concurrency);
	INIT_WORK(&priv->bt_runtime_config, iwl_bg_bt_runtime_config);
	INIT_DELAYED_WORK(&priv->_agn.hw_roc_work, iwlagn_bg_roc_done);

	iwl_setup_scan_deferred_work(priv);

	if (priv->cfg->ops->lib->setup_deferred_work)
		priv->cfg->ops->lib->setup_deferred_work(priv);

	init_timer(&priv->statistics_periodic);
	priv->statistics_periodic.data = (unsigned long)priv;
	priv->statistics_periodic.function = iwl_bg_statistics_periodic;

	init_timer(&priv->ucode_trace);
	priv->ucode_trace.data = (unsigned long)priv;
	priv->ucode_trace.function = iwl_bg_ucode_trace;

	init_timer(&priv->watchdog);
	priv->watchdog.data = (unsigned long)priv;
	priv->watchdog.function = iwl_bg_watchdog;

	tasklet_init(&priv->irq_tasklet, (void (*)(unsigned long))
		iwl_irq_tasklet, (unsigned long)priv);
}

static void iwl_cancel_deferred_work(struct iwl_priv *priv)
{
	if (priv->cfg->ops->lib->cancel_deferred_work)
		priv->cfg->ops->lib->cancel_deferred_work(priv);

	cancel_work_sync(&priv->run_time_calib_work);
	cancel_work_sync(&priv->beacon_update);

	iwl_cancel_scan_deferred_work(priv);

	cancel_work_sync(&priv->bt_full_concurrency);
	cancel_work_sync(&priv->bt_runtime_config);

	del_timer_sync(&priv->statistics_periodic);
	del_timer_sync(&priv->ucode_trace);
}

static void iwl_init_hw_rates(struct iwl_priv *priv,
			      struct ieee80211_rate *rates)
{
	int i;

	for (i = 0; i < IWL_RATE_COUNT_LEGACY; i++) {
		rates[i].bitrate = iwl_rates[i].ieee * 5;
		rates[i].hw_value = i; /* Rate scaling will work on indexes */
		rates[i].hw_value_short = i;
		rates[i].flags = 0;
		if ((i >= IWL_FIRST_CCK_RATE) && (i <= IWL_LAST_CCK_RATE)) {
			/*
			 * If CCK != 1M then set short preamble rate flag.
			 */
			rates[i].flags |=
				(iwl_rates[i].plcp == IWL_RATE_1M_PLCP) ?
					0 : IEEE80211_RATE_SHORT_PREAMBLE;
		}
	}
}

static int iwl_init_drv(struct iwl_priv *priv)
{
	int ret;

	spin_lock_init(&priv->sta_lock);
	spin_lock_init(&priv->hcmd_lock);

	mutex_init(&priv->mutex);

	priv->ieee_channels = NULL;
	priv->ieee_rates = NULL;
	priv->band = IEEE80211_BAND_2GHZ;

	priv->iw_mode = NL80211_IFTYPE_STATION;
	priv->current_ht_config.smps = IEEE80211_SMPS_STATIC;
	priv->missed_beacon_threshold = IWL_MISSED_BEACON_THRESHOLD_DEF;
	priv->_agn.agg_tids_count = 0;

	/* initialize force reset */
	priv->force_reset[IWL_RF_RESET].reset_duration =
		IWL_DELAY_NEXT_FORCE_RF_RESET;
	priv->force_reset[IWL_FW_RESET].reset_duration =
		IWL_DELAY_NEXT_FORCE_FW_RELOAD;

	priv->rx_statistics_jiffies = jiffies;

	/* Choose which receivers/antennas to use */
	if (priv->cfg->ops->hcmd->set_rxon_chain)
		priv->cfg->ops->hcmd->set_rxon_chain(priv,
					&priv->contexts[IWL_RXON_CTX_BSS]);

	iwl_init_scan_params(priv);

	/* init bt coex */
	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist) {
		priv->kill_ack_mask = IWLAGN_BT_KILL_ACK_MASK_DEFAULT;
		priv->kill_cts_mask = IWLAGN_BT_KILL_CTS_MASK_DEFAULT;
		priv->bt_valid = IWLAGN_BT_ALL_VALID_MSK;
		priv->bt_on_thresh = BT_ON_THRESHOLD_DEF;
		priv->bt_duration = BT_DURATION_LIMIT_DEF;
		priv->dynamic_frag_thresh = BT_FRAG_THRESHOLD_DEF;
	}

	ret = iwl_init_channel_map(priv);
	if (ret) {
		IWL_ERR(priv, "initializing regulatory failed: %d\n", ret);
		goto err;
	}

	ret = iwlcore_init_geos(priv);
	if (ret) {
		IWL_ERR(priv, "initializing geos failed: %d\n", ret);
		goto err_free_channel_map;
	}
	iwl_init_hw_rates(priv, priv->ieee_rates);

	return 0;

err_free_channel_map:
	iwl_free_channel_map(priv);
err:
	return ret;
}

static void iwl_uninit_drv(struct iwl_priv *priv)
{
	iwl_calib_free_results(priv);
	iwlcore_free_geos(priv);
	iwl_free_channel_map(priv);
	kfree(priv->scan_cmd);
	kfree(priv->beacon_cmd);
}

struct ieee80211_ops iwlagn_hw_ops = {
	.tx = iwlagn_mac_tx,
	.start = iwlagn_mac_start,
	.stop = iwlagn_mac_stop,
	.add_interface = iwl_mac_add_interface,
	.remove_interface = iwl_mac_remove_interface,
	.change_interface = iwl_mac_change_interface,
	.config = iwlagn_mac_config,
	.configure_filter = iwlagn_configure_filter,
	.set_key = iwlagn_mac_set_key,
	.update_tkip_key = iwlagn_mac_update_tkip_key,
	.conf_tx = iwl_mac_conf_tx,
	.bss_info_changed = iwlagn_bss_info_changed,
	.ampdu_action = iwlagn_mac_ampdu_action,
	.hw_scan = iwl_mac_hw_scan,
	.sta_notify = iwlagn_mac_sta_notify,
	.sta_add = iwlagn_mac_sta_add,
	.sta_remove = iwl_mac_sta_remove,
	.channel_switch = iwlagn_mac_channel_switch,
	.flush = iwlagn_mac_flush,
	.tx_last_beacon = iwl_mac_tx_last_beacon,
	.remain_on_channel = iwl_mac_remain_on_channel,
	.cancel_remain_on_channel = iwl_mac_cancel_remain_on_channel,
	.offchannel_tx = iwl_mac_offchannel_tx,
	.offchannel_tx_cancel_wait = iwl_mac_offchannel_tx_cancel_wait,
	CFG80211_TESTMODE_CMD(iwl_testmode_cmd)
};

static u32 iwl_hw_detect(struct iwl_priv *priv)
{
	u8 rev_id;

	pci_read_config_byte(priv->pci_dev, PCI_REVISION_ID, &rev_id);
	IWL_DEBUG_INFO(priv, "HW Revision ID = 0x%X\n", rev_id);
	return iwl_read32(priv, CSR_HW_REV);
}

static int iwl_set_hw_params(struct iwl_priv *priv)
{
	priv->hw_params.max_rxq_size = RX_QUEUE_SIZE;
	priv->hw_params.max_rxq_log = RX_QUEUE_SIZE_LOG;
	if (iwlagn_mod_params.amsdu_size_8K)
		priv->hw_params.rx_page_order = get_order(IWL_RX_BUF_SIZE_8K);
	else
		priv->hw_params.rx_page_order = get_order(IWL_RX_BUF_SIZE_4K);

	priv->hw_params.max_beacon_itrvl = IWL_MAX_UCODE_BEACON_INTERVAL;

	if (iwlagn_mod_params.disable_11n)
		priv->cfg->sku &= ~IWL_SKU_N;

	/* Device-specific setup */
	return priv->cfg->ops->lib->set_hw_params(priv);
}

static const u8 iwlagn_bss_ac_to_fifo[] = {
	IWL_TX_FIFO_VO,
	IWL_TX_FIFO_VI,
	IWL_TX_FIFO_BE,
	IWL_TX_FIFO_BK,
};

static const u8 iwlagn_bss_ac_to_queue[] = {
	0, 1, 2, 3,
};

static const u8 iwlagn_pan_ac_to_fifo[] = {
	IWL_TX_FIFO_VO_IPAN,
	IWL_TX_FIFO_VI_IPAN,
	IWL_TX_FIFO_BE_IPAN,
	IWL_TX_FIFO_BK_IPAN,
};

static const u8 iwlagn_pan_ac_to_queue[] = {
	7, 6, 5, 4,
};

/* This function both allocates and initializes hw and priv. */
static struct ieee80211_hw *iwl_alloc_all(struct iwl_cfg *cfg)
{
	struct iwl_priv *priv;
	/* mac80211 allocates memory for this device instance, including
	 *   space for this driver's private structure */
	struct ieee80211_hw *hw;

	hw = ieee80211_alloc_hw(sizeof(struct iwl_priv), &iwlagn_hw_ops);
	if (hw == NULL) {
		pr_err("%s: Can not allocate network device\n",
		       cfg->name);
		goto out;
	}

	priv = hw->priv;
	priv->hw = hw;

out:
	return hw;
}

static int iwl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = 0, i;
	struct iwl_priv *priv;
	struct ieee80211_hw *hw;
	struct iwl_cfg *cfg = (struct iwl_cfg *)(ent->driver_data);
	unsigned long flags;
	u16 pci_cmd, num_mac;
	u32 hw_rev;

	/************************
	 * 1. Allocating HW data
	 ************************/

	hw = iwl_alloc_all(cfg);
	if (!hw) {
		err = -ENOMEM;
		goto out;
	}
	priv = hw->priv;
	/* At this point both hw and priv are allocated. */

	priv->ucode_type = UCODE_SUBTYPE_NONE_LOADED;

	/*
	 * The default context is always valid,
	 * more may be discovered when firmware
	 * is loaded.
	 */
	priv->valid_contexts = BIT(IWL_RXON_CTX_BSS);

	for (i = 0; i < NUM_IWL_RXON_CTX; i++)
		priv->contexts[i].ctxid = i;

	priv->contexts[IWL_RXON_CTX_BSS].always_active = true;
	priv->contexts[IWL_RXON_CTX_BSS].is_active = true;
	priv->contexts[IWL_RXON_CTX_BSS].rxon_cmd = REPLY_RXON;
	priv->contexts[IWL_RXON_CTX_BSS].rxon_timing_cmd = REPLY_RXON_TIMING;
	priv->contexts[IWL_RXON_CTX_BSS].rxon_assoc_cmd = REPLY_RXON_ASSOC;
	priv->contexts[IWL_RXON_CTX_BSS].qos_cmd = REPLY_QOS_PARAM;
	priv->contexts[IWL_RXON_CTX_BSS].ap_sta_id = IWL_AP_ID;
	priv->contexts[IWL_RXON_CTX_BSS].wep_key_cmd = REPLY_WEPKEY;
	priv->contexts[IWL_RXON_CTX_BSS].ac_to_fifo = iwlagn_bss_ac_to_fifo;
	priv->contexts[IWL_RXON_CTX_BSS].ac_to_queue = iwlagn_bss_ac_to_queue;
	priv->contexts[IWL_RXON_CTX_BSS].exclusive_interface_modes =
		BIT(NL80211_IFTYPE_ADHOC);
	priv->contexts[IWL_RXON_CTX_BSS].interface_modes =
		BIT(NL80211_IFTYPE_STATION);
	priv->contexts[IWL_RXON_CTX_BSS].ap_devtype = RXON_DEV_TYPE_AP;
	priv->contexts[IWL_RXON_CTX_BSS].ibss_devtype = RXON_DEV_TYPE_IBSS;
	priv->contexts[IWL_RXON_CTX_BSS].station_devtype = RXON_DEV_TYPE_ESS;
	priv->contexts[IWL_RXON_CTX_BSS].unused_devtype = RXON_DEV_TYPE_ESS;

	priv->contexts[IWL_RXON_CTX_PAN].rxon_cmd = REPLY_WIPAN_RXON;
	priv->contexts[IWL_RXON_CTX_PAN].rxon_timing_cmd = REPLY_WIPAN_RXON_TIMING;
	priv->contexts[IWL_RXON_CTX_PAN].rxon_assoc_cmd = REPLY_WIPAN_RXON_ASSOC;
	priv->contexts[IWL_RXON_CTX_PAN].qos_cmd = REPLY_WIPAN_QOS_PARAM;
	priv->contexts[IWL_RXON_CTX_PAN].ap_sta_id = IWL_AP_ID_PAN;
	priv->contexts[IWL_RXON_CTX_PAN].wep_key_cmd = REPLY_WIPAN_WEPKEY;
	priv->contexts[IWL_RXON_CTX_PAN].bcast_sta_id = IWLAGN_PAN_BCAST_ID;
	priv->contexts[IWL_RXON_CTX_PAN].station_flags = STA_FLG_PAN_STATION;
	priv->contexts[IWL_RXON_CTX_PAN].ac_to_fifo = iwlagn_pan_ac_to_fifo;
	priv->contexts[IWL_RXON_CTX_PAN].ac_to_queue = iwlagn_pan_ac_to_queue;
	priv->contexts[IWL_RXON_CTX_PAN].mcast_queue = IWL_IPAN_MCAST_QUEUE;
	priv->contexts[IWL_RXON_CTX_PAN].interface_modes =
		BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP);
#ifdef CONFIG_IWL_P2P
	priv->contexts[IWL_RXON_CTX_PAN].interface_modes |=
		BIT(NL80211_IFTYPE_P2P_CLIENT) | BIT(NL80211_IFTYPE_P2P_GO);
#endif
	priv->contexts[IWL_RXON_CTX_PAN].ap_devtype = RXON_DEV_TYPE_CP;
	priv->contexts[IWL_RXON_CTX_PAN].station_devtype = RXON_DEV_TYPE_2STA;
	priv->contexts[IWL_RXON_CTX_PAN].unused_devtype = RXON_DEV_TYPE_P2P;

	BUILD_BUG_ON(NUM_IWL_RXON_CTX != 2);

	SET_IEEE80211_DEV(hw, &pdev->dev);

	IWL_DEBUG_INFO(priv, "*** LOAD DRIVER ***\n");
	priv->cfg = cfg;
	priv->pci_dev = pdev;
	priv->inta_mask = CSR_INI_SET_MASK;

	/* is antenna coupling more than 35dB ? */
	priv->bt_ant_couple_ok =
		(iwlagn_ant_coupling > IWL_BT_ANTENNA_COUPLING_THRESHOLD) ?
		true : false;

	/* enable/disable bt channel inhibition */
	priv->bt_ch_announce = iwlagn_bt_ch_announce;
	IWL_DEBUG_INFO(priv, "BT channel inhibition is %s\n",
		       (priv->bt_ch_announce) ? "On" : "Off");

	if (iwl_alloc_traffic_mem(priv))
		IWL_ERR(priv, "Not enough memory to generate traffic log\n");

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
			err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		/* both attempts failed: */
		if (err) {
			IWL_WARN(priv, "No suitable DMA available.\n");
			goto out_pci_disable_device;
		}
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err)
		goto out_pci_disable_device;

	pci_set_drvdata(pdev, priv);


	/***********************
	 * 3. Read REV register
	 ***********************/
	priv->hw_base = pci_iomap(pdev, 0, 0);
	if (!priv->hw_base) {
		err = -ENODEV;
		goto out_pci_release_regions;
	}

	IWL_DEBUG_INFO(priv, "pci_resource_len = 0x%08llx\n",
		(unsigned long long) pci_resource_len(pdev, 0));
	IWL_DEBUG_INFO(priv, "pci_resource_base = %p\n", priv->hw_base);

	/* these spin locks will be used in apm_ops.init and EEPROM access
	 * we should init now
	 */
	spin_lock_init(&priv->reg_lock);
	spin_lock_init(&priv->lock);

	/*
	 * stop and reset the on-board processor just in case it is in a
	 * strange state ... like being left stranded by a primary kernel
	 * and this is now the kdump kernel trying to start up
	 */
	iwl_write32(priv, CSR_RESET, CSR_RESET_REG_FLAG_NEVO_RESET);

	hw_rev = iwl_hw_detect(priv);
	IWL_INFO(priv, "Detected %s, REV=0x%X\n",
		priv->cfg->name, hw_rev);

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	if (iwl_prepare_card_hw(priv)) {
		IWL_WARN(priv, "Failed, HW not ready\n");
		goto out_iounmap;
	}

	/*****************
	 * 4. Read EEPROM
	 *****************/
	/* Read the EEPROM */
	err = iwl_eeprom_init(priv, hw_rev);
	if (err) {
		IWL_ERR(priv, "Unable to init EEPROM\n");
		goto out_iounmap;
	}
	err = iwl_eeprom_check_version(priv);
	if (err)
		goto out_free_eeprom;

	err = iwl_eeprom_check_sku(priv);
	if (err)
		goto out_free_eeprom;

	/* extract MAC Address */
	iwl_eeprom_get_mac(priv, priv->addresses[0].addr);
	IWL_DEBUG_INFO(priv, "MAC address: %pM\n", priv->addresses[0].addr);
	priv->hw->wiphy->addresses = priv->addresses;
	priv->hw->wiphy->n_addresses = 1;
	num_mac = iwl_eeprom_query16(priv, EEPROM_NUM_MAC_ADDRESS);
	if (num_mac > 1) {
		memcpy(priv->addresses[1].addr, priv->addresses[0].addr,
		       ETH_ALEN);
		priv->addresses[1].addr[5]++;
		priv->hw->wiphy->n_addresses++;
	}

	/************************
	 * 5. Setup HW constants
	 ************************/
	if (iwl_set_hw_params(priv)) {
		IWL_ERR(priv, "failed to set hw parameters\n");
		goto out_free_eeprom;
	}

	/*******************
	 * 6. Setup priv
	 *******************/

	err = iwl_init_drv(priv);
	if (err)
		goto out_free_eeprom;
	/* At this point both hw and priv are initialized. */

	/********************
	 * 7. Setup services
	 ********************/
	spin_lock_irqsave(&priv->lock, flags);
	iwl_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	pci_enable_msi(priv->pci_dev);

	iwl_alloc_isr_ict(priv);

	err = request_irq(priv->pci_dev->irq, iwl_isr_ict,
			  IRQF_SHARED, DRV_NAME, priv);
	if (err) {
		IWL_ERR(priv, "Error allocating IRQ %d\n", priv->pci_dev->irq);
		goto out_disable_msi;
	}

	iwl_setup_deferred_work(priv);
	iwl_setup_rx_handlers(priv);
	iwl_testmode_init(priv);

	/*********************************************
	 * 8. Enable interrupts and read RFKILL state
	 *********************************************/

	/* enable rfkill interrupt: hw bug w/a */
	pci_read_config_word(priv->pci_dev, PCI_COMMAND, &pci_cmd);
	if (pci_cmd & PCI_COMMAND_INTX_DISABLE) {
		pci_cmd &= ~PCI_COMMAND_INTX_DISABLE;
		pci_write_config_word(priv->pci_dev, PCI_COMMAND, pci_cmd);
	}

	iwl_enable_rfkill_int(priv);

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (iwl_read32(priv, CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		set_bit(STATUS_RF_KILL_HW, &priv->status);

	wiphy_rfkill_set_hw_state(priv->hw->wiphy,
		test_bit(STATUS_RF_KILL_HW, &priv->status));

	iwl_power_initialize(priv);
	iwl_tt_initialize(priv);

	init_completion(&priv->_agn.firmware_loading_complete);

	err = iwl_request_firmware(priv, true);
	if (err)
		goto out_destroy_workqueue;

	return 0;

 out_destroy_workqueue:
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;
	free_irq(priv->pci_dev->irq, priv);
	iwl_free_isr_ict(priv);
 out_disable_msi:
	pci_disable_msi(priv->pci_dev);
	iwl_uninit_drv(priv);
 out_free_eeprom:
	iwl_eeprom_free(priv);
 out_iounmap:
	pci_iounmap(pdev, priv->hw_base);
 out_pci_release_regions:
	pci_set_drvdata(pdev, NULL);
	pci_release_regions(pdev);
 out_pci_disable_device:
	pci_disable_device(pdev);
 out_ieee80211_free_hw:
	iwl_free_traffic_mem(priv);
	ieee80211_free_hw(priv->hw);
 out:
	return err;
}

static void __devexit iwl_pci_remove(struct pci_dev *pdev)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);
	unsigned long flags;

	if (!priv)
		return;

	wait_for_completion(&priv->_agn.firmware_loading_complete);

	IWL_DEBUG_INFO(priv, "*** UNLOAD DRIVER ***\n");

	iwl_dbgfs_unregister(priv);
	sysfs_remove_group(&pdev->dev.kobj, &iwl_attribute_group);

	/* ieee80211_unregister_hw call wil cause iwl_mac_stop to
	 * to be called and iwl_down since we are removing the device
	 * we need to set STATUS_EXIT_PENDING bit.
	 */
	set_bit(STATUS_EXIT_PENDING, &priv->status);

	iwl_testmode_cleanup(priv);
	iwl_leds_exit(priv);

	if (priv->mac80211_registered) {
		ieee80211_unregister_hw(priv->hw);
		priv->mac80211_registered = 0;
	}

	/* Reset to low power before unloading driver. */
	iwl_apm_stop(priv);

	iwl_tt_exit(priv);

	/* make sure we flush any pending irq or
	 * tasklet for the driver
	 */
	spin_lock_irqsave(&priv->lock, flags);
	iwl_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_synchronize_irq(priv);

	iwl_dealloc_ucode_pci(priv);

	if (priv->rxq.bd)
		iwlagn_rx_queue_free(priv, &priv->rxq);
	iwlagn_hw_txq_ctx_free(priv);

	iwl_eeprom_free(priv);


	/*netif_stop_queue(dev); */
	flush_workqueue(priv->workqueue);

	/* ieee80211_unregister_hw calls iwl_mac_stop, which flushes
	 * priv->workqueue... so we can't take down the workqueue
	 * until now... */
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;
	iwl_free_traffic_mem(priv);

	free_irq(priv->pci_dev->irq, priv);
	pci_disable_msi(priv->pci_dev);
	pci_iounmap(pdev, priv->hw_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	iwl_uninit_drv(priv);

	iwl_free_isr_ict(priv);

	dev_kfree_skb(priv->beacon_skb);

	ieee80211_free_hw(priv->hw);
}


/*****************************************************************************
 *
 * driver and module entry point
 *
 *****************************************************************************/

/* Hardware specific file defines the PCI IDs table for that hardware module */
static DEFINE_PCI_DEVICE_TABLE(iwl_hw_card_ids) = {
	{IWL_PCI_DEVICE(0x4232, 0x1201, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1301, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1204, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1304, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1205, iwl5100_bgn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1305, iwl5100_bgn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1206, iwl5100_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1306, iwl5100_abg_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1221, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1321, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1224, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1324, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1225, iwl5100_bgn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1325, iwl5100_bgn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1226, iwl5100_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1326, iwl5100_abg_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1211, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1311, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1214, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1314, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1215, iwl5100_bgn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1315, iwl5100_bgn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1216, iwl5100_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1316, iwl5100_abg_cfg)}, /* Half Mini Card */

/* 5300 Series WiFi */
	{IWL_PCI_DEVICE(0x4235, 0x1021, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1121, iwl5300_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1024, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1124, iwl5300_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1001, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1101, iwl5300_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1004, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1104, iwl5300_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1011, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1111, iwl5300_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1014, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1114, iwl5300_agn_cfg)}, /* Half Mini Card */

/* 5350 Series WiFi/WiMax */
	{IWL_PCI_DEVICE(0x423A, 0x1001, iwl5350_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423A, 0x1021, iwl5350_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423B, 0x1011, iwl5350_agn_cfg)}, /* Mini Card */

/* 5150 Series Wifi/WiMax */
	{IWL_PCI_DEVICE(0x423C, 0x1201, iwl5150_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1301, iwl5150_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1206, iwl5150_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1306, iwl5150_abg_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1221, iwl5150_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1321, iwl5150_agn_cfg)}, /* Half Mini Card */

	{IWL_PCI_DEVICE(0x423D, 0x1211, iwl5150_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1311, iwl5150_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1216, iwl5150_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1316, iwl5150_abg_cfg)}, /* Half Mini Card */

/* 6x00 Series */
	{IWL_PCI_DEVICE(0x422B, 0x1101, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x422B, 0x1121, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1301, iwl6000i_2agn_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1306, iwl6000i_2abg_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1307, iwl6000i_2bg_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1321, iwl6000i_2agn_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1326, iwl6000i_2abg_cfg)},
	{IWL_PCI_DEVICE(0x4238, 0x1111, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x4239, 0x1311, iwl6000i_2agn_cfg)},
	{IWL_PCI_DEVICE(0x4239, 0x1316, iwl6000i_2abg_cfg)},

/* 6x05 Series */
	{IWL_PCI_DEVICE(0x0082, 0x1301, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1306, iwl6005_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1307, iwl6005_2bg_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1321, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1326, iwl6005_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0x1311, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0x1316, iwl6005_2abg_cfg)},

/* 6x30 Series */
	{IWL_PCI_DEVICE(0x008A, 0x5305, iwl1030_bgn_cfg)},
	{IWL_PCI_DEVICE(0x008A, 0x5307, iwl1030_bg_cfg)},
	{IWL_PCI_DEVICE(0x008A, 0x5325, iwl1030_bgn_cfg)},
	{IWL_PCI_DEVICE(0x008A, 0x5327, iwl1030_bg_cfg)},
	{IWL_PCI_DEVICE(0x008B, 0x5315, iwl1030_bgn_cfg)},
	{IWL_PCI_DEVICE(0x008B, 0x5317, iwl1030_bg_cfg)},
	{IWL_PCI_DEVICE(0x0090, 0x5211, iwl6030_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0090, 0x5215, iwl6030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0090, 0x5216, iwl6030_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5201, iwl6030_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5205, iwl6030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5206, iwl6030_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5207, iwl6030_2bg_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5221, iwl6030_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5225, iwl6030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5226, iwl6030_2abg_cfg)},

/* 6x50 WiFi/WiMax Series */
	{IWL_PCI_DEVICE(0x0087, 0x1301, iwl6050_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1306, iwl6050_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1321, iwl6050_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1326, iwl6050_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0089, 0x1311, iwl6050_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0089, 0x1316, iwl6050_2abg_cfg)},

/* 6150 WiFi/WiMax Series */
	{IWL_PCI_DEVICE(0x0885, 0x1305, iwl6150_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0885, 0x1307, iwl6150_bg_cfg)},
	{IWL_PCI_DEVICE(0x0885, 0x1325, iwl6150_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0885, 0x1327, iwl6150_bg_cfg)},
	{IWL_PCI_DEVICE(0x0886, 0x1315, iwl6150_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0886, 0x1317, iwl6150_bg_cfg)},

/* 1000 Series WiFi */
	{IWL_PCI_DEVICE(0x0083, 0x1205, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1305, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1225, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1325, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1215, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1315, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1206, iwl1000_bg_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1306, iwl1000_bg_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1226, iwl1000_bg_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1326, iwl1000_bg_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1216, iwl1000_bg_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1316, iwl1000_bg_cfg)},

/* 100 Series WiFi */
	{IWL_PCI_DEVICE(0x08AE, 0x1005, iwl100_bgn_cfg)},
	{IWL_PCI_DEVICE(0x08AE, 0x1007, iwl100_bg_cfg)},
	{IWL_PCI_DEVICE(0x08AF, 0x1015, iwl100_bgn_cfg)},
	{IWL_PCI_DEVICE(0x08AF, 0x1017, iwl100_bg_cfg)},
	{IWL_PCI_DEVICE(0x08AE, 0x1025, iwl100_bgn_cfg)},
	{IWL_PCI_DEVICE(0x08AE, 0x1027, iwl100_bg_cfg)},

/* 130 Series WiFi */
	{IWL_PCI_DEVICE(0x0896, 0x5005, iwl130_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0896, 0x5007, iwl130_bg_cfg)},
	{IWL_PCI_DEVICE(0x0897, 0x5015, iwl130_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0897, 0x5017, iwl130_bg_cfg)},
	{IWL_PCI_DEVICE(0x0896, 0x5025, iwl130_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0896, 0x5027, iwl130_bg_cfg)},

/* 2x00 Series */
	{IWL_PCI_DEVICE(0x0890, 0x4022, iwl2000_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0891, 0x4222, iwl2000_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0890, 0x4422, iwl2000_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0890, 0x4026, iwl2000_2bg_cfg)},
	{IWL_PCI_DEVICE(0x0891, 0x4226, iwl2000_2bg_cfg)},
	{IWL_PCI_DEVICE(0x0890, 0x4426, iwl2000_2bg_cfg)},

/* 2x30 Series */
	{IWL_PCI_DEVICE(0x0887, 0x4062, iwl2030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0888, 0x4262, iwl2030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0887, 0x4462, iwl2030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0887, 0x4066, iwl2030_2bg_cfg)},
	{IWL_PCI_DEVICE(0x0888, 0x4266, iwl2030_2bg_cfg)},
	{IWL_PCI_DEVICE(0x0887, 0x4466, iwl2030_2bg_cfg)},

/* 6x35 Series */
	{IWL_PCI_DEVICE(0x088E, 0x4060, iwl6035_2agn_cfg)},
	{IWL_PCI_DEVICE(0x088F, 0x4260, iwl6035_2agn_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4460, iwl6035_2agn_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4064, iwl6035_2abg_cfg)},
	{IWL_PCI_DEVICE(0x088F, 0x4264, iwl6035_2abg_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4464, iwl6035_2abg_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4066, iwl6035_2bg_cfg)},
	{IWL_PCI_DEVICE(0x088F, 0x4266, iwl6035_2bg_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4466, iwl6035_2bg_cfg)},

/* 105 Series */
	{IWL_PCI_DEVICE(0x0894, 0x0022, iwl105_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0895, 0x0222, iwl105_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0894, 0x0422, iwl105_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0894, 0x0026, iwl105_bg_cfg)},
	{IWL_PCI_DEVICE(0x0895, 0x0226, iwl105_bg_cfg)},
	{IWL_PCI_DEVICE(0x0894, 0x0426, iwl105_bg_cfg)},

/* 135 Series */
	{IWL_PCI_DEVICE(0x0892, 0x0062, iwl135_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0893, 0x0262, iwl135_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0892, 0x0462, iwl135_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0892, 0x0066, iwl135_bg_cfg)},
	{IWL_PCI_DEVICE(0x0893, 0x0266, iwl135_bg_cfg)},
	{IWL_PCI_DEVICE(0x0892, 0x0466, iwl135_bg_cfg)},

	{0}
};
MODULE_DEVICE_TABLE(pci, iwl_hw_card_ids);

static struct pci_driver iwl_driver = {
	.name = DRV_NAME,
	.id_table = iwl_hw_card_ids,
	.probe = iwl_pci_probe,
	.remove = __devexit_p(iwl_pci_remove),
	.driver.pm = IWL_PM_OPS,
};

static int __init iwl_init(void)
{

	int ret;
	pr_info(DRV_DESCRIPTION ", " DRV_VERSION "\n");
	pr_info(DRV_COPYRIGHT "\n");

	ret = iwlagn_rate_control_register();
	if (ret) {
		pr_err("Unable to register rate control algorithm: %d\n", ret);
		return ret;
	}

	ret = pci_register_driver(&iwl_driver);
	if (ret) {
		pr_err("Unable to initialize PCI module\n");
		goto error_register;
	}

	return ret;

error_register:
	iwlagn_rate_control_unregister();
	return ret;
}

static void __exit iwl_exit(void)
{
	pci_unregister_driver(&iwl_driver);
	iwlagn_rate_control_unregister();
}

module_exit(iwl_exit);
module_init(iwl_init);

#ifdef CONFIG_IWLWIFI_DEBUG
module_param_named(debug, iwl_debug_level, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "debug output mask");
#endif

module_param_named(swcrypto, iwlagn_mod_params.sw_crypto, int, S_IRUGO);
MODULE_PARM_DESC(swcrypto, "using crypto in software (default 0 [hardware])");
module_param_named(queues_num, iwlagn_mod_params.num_of_queues, int, S_IRUGO);
MODULE_PARM_DESC(queues_num, "number of hw queues.");
module_param_named(11n_disable, iwlagn_mod_params.disable_11n, int, S_IRUGO);
MODULE_PARM_DESC(11n_disable, "disable 11n functionality");
module_param_named(amsdu_size_8K, iwlagn_mod_params.amsdu_size_8K,
		   int, S_IRUGO);
MODULE_PARM_DESC(amsdu_size_8K, "enable 8K amsdu size");
module_param_named(fw_restart, iwlagn_mod_params.restart_fw, int, S_IRUGO);
MODULE_PARM_DESC(fw_restart, "restart firmware in case of error");

module_param_named(ucode_alternative, iwlagn_wanted_ucode_alternative, int,
		   S_IRUGO);
MODULE_PARM_DESC(ucode_alternative,
		 "specify ucode alternative to use from ucode file");

module_param_named(antenna_coupling, iwlagn_ant_coupling, int, S_IRUGO);
MODULE_PARM_DESC(antenna_coupling,
		 "specify antenna coupling in dB (defualt: 0 dB)");

module_param_named(bt_ch_inhibition, iwlagn_bt_ch_announce, bool, S_IRUGO);
MODULE_PARM_DESC(bt_ch_inhibition,
		 "Disable BT channel inhibition (default: enable)");

module_param_named(plcp_check, iwlagn_mod_params.plcp_check, bool, S_IRUGO);
MODULE_PARM_DESC(plcp_check, "Check plcp health (default: 1 [enabled])");

module_param_named(ack_check, iwlagn_mod_params.ack_check, bool, S_IRUGO);
MODULE_PARM_DESC(ack_check, "Check ack health (default: 0 [disabled])");
