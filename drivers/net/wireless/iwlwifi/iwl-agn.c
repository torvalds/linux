/******************************************************************************
 *
 * Copyright(c) 2003 - 2010 Intel Corporation. All rights reserved.
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
#include "iwl-calib.h"


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
MODULE_ALIAS("iwl4965");

/*************** STATION TABLE MANAGEMENT ****
 * mac80211 should be examined to determine if sta_info is duplicating
 * the functionality provided here
 */

/**************************************************************/

/**
 * iwl_commit_rxon - commit staging_rxon to hardware
 *
 * The RXON command in staging_rxon is committed to the hardware and
 * the active_rxon structure is updated with the new data.  This
 * function correctly transitions out of the RXON_ASSOC_MSK state if
 * a HW tune is required based on the RXON structure changes.
 */
int iwl_commit_rxon(struct iwl_priv *priv)
{
	/* cast away the const for active_rxon in this function */
	struct iwl_rxon_cmd *active_rxon = (void *)&priv->active_rxon;
	int ret;
	bool new_assoc =
		!!(priv->staging_rxon.filter_flags & RXON_FILTER_ASSOC_MSK);

	if (!iwl_is_alive(priv))
		return -EBUSY;

	/* always get timestamp with Rx frame */
	priv->staging_rxon.flags |= RXON_FLG_TSF2HOST_MSK;

	ret = iwl_check_rxon_cmd(priv);
	if (ret) {
		IWL_ERR(priv, "Invalid RXON configuration.  Not committing.\n");
		return -EINVAL;
	}

	/*
	 * receive commit_rxon request
	 * abort any previous channel switch if still in process
	 */
	if (priv->switch_rxon.switch_in_progress &&
	    (priv->switch_rxon.channel != priv->staging_rxon.channel)) {
		IWL_DEBUG_11H(priv, "abort channel switch on %d\n",
		      le16_to_cpu(priv->switch_rxon.channel));
		priv->switch_rxon.switch_in_progress = false;
	}

	/* If we don't need to send a full RXON, we can use
	 * iwl_rxon_assoc_cmd which is used to reconfigure filter
	 * and other flags for the current radio configuration. */
	if (!iwl_full_rxon_required(priv)) {
		ret = iwl_send_rxon_assoc(priv);
		if (ret) {
			IWL_ERR(priv, "Error setting RXON_ASSOC (%d)\n", ret);
			return ret;
		}

		memcpy(active_rxon, &priv->staging_rxon, sizeof(*active_rxon));
		iwl_print_rx_config_cmd(priv);
		return 0;
	}

	/* station table will be cleared */
	priv->assoc_station_added = 0;

	/* If we are currently associated and the new config requires
	 * an RXON_ASSOC and the new config wants the associated mask enabled,
	 * we must clear the associated from the active configuration
	 * before we apply the new config */
	if (iwl_is_associated(priv) && new_assoc) {
		IWL_DEBUG_INFO(priv, "Toggling associated bit on current RXON\n");
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;

		ret = iwl_send_cmd_pdu(priv, REPLY_RXON,
				      sizeof(struct iwl_rxon_cmd),
				      &priv->active_rxon);

		/* If the mask clearing failed then we set
		 * active_rxon back to what it was previously */
		if (ret) {
			active_rxon->filter_flags |= RXON_FILTER_ASSOC_MSK;
			IWL_ERR(priv, "Error clearing ASSOC_MSK (%d)\n", ret);
			return ret;
		}
	}

	IWL_DEBUG_INFO(priv, "Sending RXON\n"
		       "* with%s RXON_FILTER_ASSOC_MSK\n"
		       "* channel = %d\n"
		       "* bssid = %pM\n",
		       (new_assoc ? "" : "out"),
		       le16_to_cpu(priv->staging_rxon.channel),
		       priv->staging_rxon.bssid_addr);

	iwl_set_rxon_hwcrypto(priv, !priv->cfg->mod_params->sw_crypto);

	/* Apply the new configuration
	 * RXON unassoc clears the station table in uCode, send it before
	 * we add the bcast station. If assoc bit is set, we will send RXON
	 * after having added the bcast and bssid station.
	 */
	if (!new_assoc) {
		ret = iwl_send_cmd_pdu(priv, REPLY_RXON,
			      sizeof(struct iwl_rxon_cmd), &priv->staging_rxon);
		if (ret) {
			IWL_ERR(priv, "Error setting new RXON (%d)\n", ret);
			return ret;
		}
		memcpy(active_rxon, &priv->staging_rxon, sizeof(*active_rxon));
	}

	iwl_clear_stations_table(priv);

	priv->start_calib = 0;

	/* Add the broadcast address so we can send broadcast frames */
	priv->cfg->ops->lib->add_bcast_station(priv);


	/* If we have set the ASSOC_MSK and we are in BSS mode then
	 * add the IWL_AP_ID to the station rate table */
	if (new_assoc) {
		if (priv->iw_mode == NL80211_IFTYPE_STATION) {
			ret = iwl_rxon_add_station(priv,
					   priv->active_rxon.bssid_addr, 1);
			if (ret == IWL_INVALID_STATION) {
				IWL_ERR(priv,
					"Error adding AP address for TX.\n");
				return -EIO;
			}
			priv->assoc_station_added = 1;
			if (priv->default_wep_key &&
			    iwl_send_static_wepkey_cmd(priv, 0))
				IWL_ERR(priv,
					"Could not send WEP static key.\n");
		}

		/*
		 * allow CTS-to-self if possible for new association.
		 * this is relevant only for 5000 series and up,
		 * but will not damage 4965
		 */
		priv->staging_rxon.flags |= RXON_FLG_SELF_CTS_EN;

		/* Apply the new configuration
		 * RXON assoc doesn't clear the station table in uCode,
		 */
		ret = iwl_send_cmd_pdu(priv, REPLY_RXON,
			      sizeof(struct iwl_rxon_cmd), &priv->staging_rxon);
		if (ret) {
			IWL_ERR(priv, "Error setting new RXON (%d)\n", ret);
			return ret;
		}
		memcpy(active_rxon, &priv->staging_rxon, sizeof(*active_rxon));
	}
	iwl_print_rx_config_cmd(priv);

	iwl_init_sensitivity(priv);

	/* If we issue a new RXON command which required a tune then we must
	 * send a new TXPOWER command or we won't be able to Tx any frames */
	ret = iwl_set_tx_power(priv, priv->tx_power_user_lmt, true);
	if (ret) {
		IWL_ERR(priv, "Error sending TX power (%d)\n", ret);
		return ret;
	}

	return 0;
}

void iwl_update_chain_flags(struct iwl_priv *priv)
{

	if (priv->cfg->ops->hcmd->set_rxon_chain)
		priv->cfg->ops->hcmd->set_rxon_chain(priv);
	iwlcore_commit_rxon(priv);
}

static void iwl_clear_free_frames(struct iwl_priv *priv)
{
	struct list_head *element;

	IWL_DEBUG_INFO(priv, "%d frames on pre-allocated heap on clear.\n",
		       priv->frames_count);

	while (!list_empty(&priv->free_frames)) {
		element = priv->free_frames.next;
		list_del(element);
		kfree(list_entry(element, struct iwl_frame, list));
		priv->frames_count--;
	}

	if (priv->frames_count) {
		IWL_WARN(priv, "%d frames still in use.  Did we lose one?\n",
			    priv->frames_count);
		priv->frames_count = 0;
	}
}

static struct iwl_frame *iwl_get_free_frame(struct iwl_priv *priv)
{
	struct iwl_frame *frame;
	struct list_head *element;
	if (list_empty(&priv->free_frames)) {
		frame = kzalloc(sizeof(*frame), GFP_KERNEL);
		if (!frame) {
			IWL_ERR(priv, "Could not allocate frame!\n");
			return NULL;
		}

		priv->frames_count++;
		return frame;
	}

	element = priv->free_frames.next;
	list_del(element);
	return list_entry(element, struct iwl_frame, list);
}

static void iwl_free_frame(struct iwl_priv *priv, struct iwl_frame *frame)
{
	memset(frame, 0, sizeof(*frame));
	list_add(&frame->list, &priv->free_frames);
}

static u32 iwl_fill_beacon_frame(struct iwl_priv *priv,
					  struct ieee80211_hdr *hdr,
					  int left)
{
	if (!iwl_is_associated(priv) || !priv->ibss_beacon ||
	    ((priv->iw_mode != NL80211_IFTYPE_ADHOC) &&
	     (priv->iw_mode != NL80211_IFTYPE_AP)))
		return 0;

	if (priv->ibss_beacon->len > left)
		return 0;

	memcpy(hdr, priv->ibss_beacon->data, priv->ibss_beacon->len);

	return priv->ibss_beacon->len;
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

static unsigned int iwl_hw_get_beacon_cmd(struct iwl_priv *priv,
				       struct iwl_frame *frame)
{
	struct iwl_tx_beacon_cmd *tx_beacon_cmd;
	u32 frame_size;
	u32 rate_flags;
	u32 rate;
	/*
	 * We have to set up the TX command, the TX Beacon command, and the
	 * beacon contents.
	 */

	/* Initialize memory */
	tx_beacon_cmd = &frame->u.beacon;
	memset(tx_beacon_cmd, 0, sizeof(*tx_beacon_cmd));

	/* Set up TX beacon contents */
	frame_size = iwl_fill_beacon_frame(priv, tx_beacon_cmd->frame,
				sizeof(frame->u) - sizeof(*tx_beacon_cmd));
	if (WARN_ON_ONCE(frame_size > MAX_MPDU_SIZE))
		return 0;

	/* Set up TX command fields */
	tx_beacon_cmd->tx.len = cpu_to_le16((u16)frame_size);
	tx_beacon_cmd->tx.sta_id = priv->hw_params.bcast_sta_id;
	tx_beacon_cmd->tx.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;
	tx_beacon_cmd->tx.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK |
		TX_CMD_FLG_TSF_MSK | TX_CMD_FLG_STA_RATE_MSK;

	/* Set up TX beacon command fields */
	iwl_set_beacon_tim(priv, tx_beacon_cmd, (u8 *)tx_beacon_cmd->frame,
			frame_size);

	/* Set up packet rate and flags */
	rate = iwl_rate_get_lowest_plcp(priv);
	priv->mgmt_tx_ant = iwl_toggle_tx_ant(priv, priv->mgmt_tx_ant);
	rate_flags = iwl_ant_idx_to_flags(priv->mgmt_tx_ant);
	if ((rate >= IWL_FIRST_CCK_RATE) && (rate <= IWL_LAST_CCK_RATE))
		rate_flags |= RATE_MCS_CCK_MSK;
	tx_beacon_cmd->tx.rate_n_flags = iwl_hw_set_rate_n_flags(rate,
			rate_flags);

	return sizeof(*tx_beacon_cmd) + frame_size;
}
static int iwl_send_beacon_cmd(struct iwl_priv *priv)
{
	struct iwl_frame *frame;
	unsigned int frame_size;
	int rc;

	frame = iwl_get_free_frame(priv);
	if (!frame) {
		IWL_ERR(priv, "Could not obtain free frame buffer for beacon "
			  "command.\n");
		return -ENOMEM;
	}

	frame_size = iwl_hw_get_beacon_cmd(priv, frame);
	if (!frame_size) {
		IWL_ERR(priv, "Error configuring the beacon command\n");
		iwl_free_frame(priv, frame);
		return -EINVAL;
	}

	rc = iwl_send_cmd_pdu(priv, REPLY_TX_BEACON, frame_size,
			      &frame->u.cmd[0]);

	iwl_free_frame(priv, frame);

	return rc;
}

static inline dma_addr_t iwl_tfd_tb_get_addr(struct iwl_tfd *tfd, u8 idx)
{
	struct iwl_tfd_tb *tb = &tfd->tbs[idx];

	dma_addr_t addr = get_unaligned_le32(&tb->lo);
	if (sizeof(dma_addr_t) > sizeof(u32))
		addr |=
		((dma_addr_t)(le16_to_cpu(tb->hi_n_len) & 0xF) << 16) << 16;

	return addr;
}

static inline u16 iwl_tfd_tb_get_len(struct iwl_tfd *tfd, u8 idx)
{
	struct iwl_tfd_tb *tb = &tfd->tbs[idx];

	return le16_to_cpu(tb->hi_n_len) >> 4;
}

static inline void iwl_tfd_set_tb(struct iwl_tfd *tfd, u8 idx,
				  dma_addr_t addr, u16 len)
{
	struct iwl_tfd_tb *tb = &tfd->tbs[idx];
	u16 hi_n_len = len << 4;

	put_unaligned_le32(addr, &tb->lo);
	if (sizeof(dma_addr_t) > sizeof(u32))
		hi_n_len |= ((addr >> 16) >> 16) & 0xF;

	tb->hi_n_len = cpu_to_le16(hi_n_len);

	tfd->num_tbs = idx + 1;
}

static inline u8 iwl_tfd_get_num_tbs(struct iwl_tfd *tfd)
{
	return tfd->num_tbs & 0x1f;
}

/**
 * iwl_hw_txq_free_tfd - Free all chunks referenced by TFD [txq->q.read_ptr]
 * @priv - driver private data
 * @txq - tx queue
 *
 * Does NOT advance any TFD circular buffer read/write indexes
 * Does NOT free the TFD itself (which is within circular buffer)
 */
void iwl_hw_txq_free_tfd(struct iwl_priv *priv, struct iwl_tx_queue *txq)
{
	struct iwl_tfd *tfd_tmp = (struct iwl_tfd *)txq->tfds;
	struct iwl_tfd *tfd;
	struct pci_dev *dev = priv->pci_dev;
	int index = txq->q.read_ptr;
	int i;
	int num_tbs;

	tfd = &tfd_tmp[index];

	/* Sanity check on number of chunks */
	num_tbs = iwl_tfd_get_num_tbs(tfd);

	if (num_tbs >= IWL_NUM_OF_TBS) {
		IWL_ERR(priv, "Too many chunks: %i\n", num_tbs);
		/* @todo issue fatal error, it is quite serious situation */
		return;
	}

	/* Unmap tx_cmd */
	if (num_tbs)
		pci_unmap_single(dev,
				pci_unmap_addr(&txq->meta[index], mapping),
				pci_unmap_len(&txq->meta[index], len),
				PCI_DMA_BIDIRECTIONAL);

	/* Unmap chunks, if any. */
	for (i = 1; i < num_tbs; i++) {
		pci_unmap_single(dev, iwl_tfd_tb_get_addr(tfd, i),
				iwl_tfd_tb_get_len(tfd, i), PCI_DMA_TODEVICE);

		if (txq->txb) {
			dev_kfree_skb(txq->txb[txq->q.read_ptr].skb[i - 1]);
			txq->txb[txq->q.read_ptr].skb[i - 1] = NULL;
		}
	}
}

int iwl_hw_txq_attach_buf_to_tfd(struct iwl_priv *priv,
				 struct iwl_tx_queue *txq,
				 dma_addr_t addr, u16 len,
				 u8 reset, u8 pad)
{
	struct iwl_queue *q;
	struct iwl_tfd *tfd, *tfd_tmp;
	u32 num_tbs;

	q = &txq->q;
	tfd_tmp = (struct iwl_tfd *)txq->tfds;
	tfd = &tfd_tmp[q->write_ptr];

	if (reset)
		memset(tfd, 0, sizeof(*tfd));

	num_tbs = iwl_tfd_get_num_tbs(tfd);

	/* Each TFD can point to a maximum 20 Tx buffers */
	if (num_tbs >= IWL_NUM_OF_TBS) {
		IWL_ERR(priv, "Error can not send more than %d chunks\n",
			  IWL_NUM_OF_TBS);
		return -EINVAL;
	}

	BUG_ON(addr & ~DMA_BIT_MASK(36));
	if (unlikely(addr & ~IWL_TX_DMA_MASK))
		IWL_ERR(priv, "Unaligned address = %llx\n",
			  (unsigned long long)addr);

	iwl_tfd_set_tb(tfd, num_tbs, addr, len);

	return 0;
}

/*
 * Tell nic where to find circular buffer of Tx Frame Descriptors for
 * given Tx queue, and enable the DMA channel used for that queue.
 *
 * 4965 supports up to 16 Tx queues in DRAM, mapped to up to 8 Tx DMA
 * channels supported in hardware.
 */
int iwl_hw_tx_queue_init(struct iwl_priv *priv,
			 struct iwl_tx_queue *txq)
{
	int txq_id = txq->q.id;

	/* Circular buffer (TFD queue in DRAM) physical base address */
	iwl_write_direct32(priv, FH_MEM_CBBC_QUEUE(txq_id),
			     txq->q.dma_addr >> 8);

	return 0;
}

/******************************************************************************
 *
 * Generic RX handler implementations
 *
 ******************************************************************************/
static void iwl_rx_reply_alive(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_alive_resp *palive;
	struct delayed_work *pwork;

	palive = &pkt->u.alive_frame;

	IWL_DEBUG_INFO(priv, "Alive ucode status 0x%08X revision "
		       "0x%01X 0x%01X\n",
		       palive->is_valid, palive->ver_type,
		       palive->ver_subtype);

	if (palive->ver_subtype == INITIALIZE_SUBTYPE) {
		IWL_DEBUG_INFO(priv, "Initialization Alive received.\n");
		memcpy(&priv->card_alive_init,
		       &pkt->u.alive_frame,
		       sizeof(struct iwl_init_alive_resp));
		pwork = &priv->init_alive_start;
	} else {
		IWL_DEBUG_INFO(priv, "Runtime Alive received.\n");
		memcpy(&priv->card_alive, &pkt->u.alive_frame,
		       sizeof(struct iwl_alive_resp));
		pwork = &priv->alive_start;
	}

	/* We delay the ALIVE response by 5ms to
	 * give the HW RF Kill time to activate... */
	if (palive->is_valid == UCODE_VALID_OK)
		queue_delayed_work(priv->workqueue, pwork,
				   msecs_to_jiffies(5));
	else
		IWL_WARN(priv, "uCode did not respond OK.\n");
}

static void iwl_bg_beacon_update(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, beacon_update);
	struct sk_buff *beacon;

	/* Pull updated AP beacon from mac80211. will fail if not in AP mode */
	beacon = ieee80211_beacon_get(priv->hw, priv->vif);

	if (!beacon) {
		IWL_ERR(priv, "update beacon failed\n");
		return;
	}

	mutex_lock(&priv->mutex);
	/* new beacon skb is allocated every time; dispose previous.*/
	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = beacon;
	mutex_unlock(&priv->mutex);

	iwl_send_beacon_cmd(priv);
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
	_iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR, ptr);
	rmb();

	/*
	 * "time" is actually "data" for mode 0 (no timestamp).
	 * place event id # at far right for easier visual parsing.
	 */
	for (i = 0; i < num_events; i++) {
		ev = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		time = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (mode == 0) {
			trace_iwlwifi_dev_ucode_cont_event(priv,
							0, time, ev);
		} else {
			data = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
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

	if (priv->ucode_type == UCODE_INIT)
		base = le32_to_cpu(priv->card_alive_init.error_event_table_ptr);
	else
		base = le32_to_cpu(priv->card_alive.log_event_table_ptr);
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

static void iwl_rx_beacon_notif(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl4965_beacon_notif *beacon =
		(struct iwl4965_beacon_notif *)pkt->u.raw;
	u8 rate = iwl_hw_get_rate(beacon->beacon_notify_hdr.rate_n_flags);

	IWL_DEBUG_RX(priv, "beacon status %x retries %d iss %d "
		"tsf %d %d rate %d\n",
		le32_to_cpu(beacon->beacon_notify_hdr.u.status) & TX_STATUS_MSK,
		beacon->beacon_notify_hdr.failure_frame,
		le32_to_cpu(beacon->ibss_mgr_status),
		le32_to_cpu(beacon->high_tsf),
		le32_to_cpu(beacon->low_tsf), rate);
#endif

	if ((priv->iw_mode == NL80211_IFTYPE_AP) &&
	    (!test_bit(STATUS_EXIT_PENDING, &priv->status)))
		queue_work(priv->workqueue, &priv->beacon_update);
}

/* Handle notification from uCode that card's power state is changing
 * due to software, hardware, or critical temperature RFKILL */
static void iwl_rx_card_state_notif(struct iwl_priv *priv,
				    struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u32 flags = le32_to_cpu(pkt->u.card_state_notif.flags);
	unsigned long status = priv->status;

	IWL_DEBUG_RF_KILL(priv, "Card state received: HW:%s SW:%s CT:%s\n",
			  (flags & HW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & SW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & CT_CARD_DISABLED) ?
			  "Reached" : "Not reached");

	if (flags & (SW_CARD_DISABLED | HW_CARD_DISABLED |
		     CT_CARD_DISABLED)) {

		iwl_write32(priv, CSR_UCODE_DRV_GP1_SET,
			    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

		iwl_write_direct32(priv, HBUS_TARG_MBX_C,
					HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED);

		if (!(flags & RXON_CARD_DISABLED)) {
			iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
				    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);
			iwl_write_direct32(priv, HBUS_TARG_MBX_C,
					HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED);
		}
		if (flags & CT_CARD_DISABLED)
			iwl_tt_enter_ct_kill(priv);
	}
	if (!(flags & CT_CARD_DISABLED))
		iwl_tt_exit_ct_kill(priv);

	if (flags & HW_CARD_DISABLED)
		set_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		clear_bit(STATUS_RF_KILL_HW, &priv->status);


	if (!(flags & RXON_CARD_DISABLED))
		iwl_scan_cancel(priv);

	if ((test_bit(STATUS_RF_KILL_HW, &status) !=
	     test_bit(STATUS_RF_KILL_HW, &priv->status)))
		wiphy_rfkill_set_hw_state(priv->hw->wiphy,
			test_bit(STATUS_RF_KILL_HW, &priv->status));
	else
		wake_up_interruptible(&priv->wait_command_queue);
}

int iwl_set_pwr_src(struct iwl_priv *priv, enum iwl_pwr_src src)
{
	if (src == IWL_PWR_SRC_VAUX) {
		if (pci_pme_capable(priv->pci_dev, PCI_D3cold))
			iwl_set_bits_mask_prph(priv, APMG_PS_CTRL_REG,
					       APMG_PS_CTRL_VAL_PWR_SRC_VAUX,
					       ~APMG_PS_CTRL_MSK_PWR_SRC);
	} else {
		iwl_set_bits_mask_prph(priv, APMG_PS_CTRL_REG,
				       APMG_PS_CTRL_VAL_PWR_SRC_VMAIN,
				       ~APMG_PS_CTRL_MSK_PWR_SRC);
	}

	return 0;
}

/**
 * iwl_setup_rx_handlers - Initialize Rx handler callbacks
 *
 * Setup the RX handlers for each of the reply types sent from the uCode
 * to the host.
 *
 * This function chains into the hardware specific files for them to setup
 * any hardware specific handlers as well.
 */
static void iwl_setup_rx_handlers(struct iwl_priv *priv)
{
	priv->rx_handlers[REPLY_ALIVE] = iwl_rx_reply_alive;
	priv->rx_handlers[REPLY_ERROR] = iwl_rx_reply_error;
	priv->rx_handlers[CHANNEL_SWITCH_NOTIFICATION] = iwl_rx_csa;
	priv->rx_handlers[SPECTRUM_MEASURE_NOTIFICATION] =
			iwl_rx_spectrum_measure_notif;
	priv->rx_handlers[PM_SLEEP_NOTIFICATION] = iwl_rx_pm_sleep_notif;
	priv->rx_handlers[PM_DEBUG_STATISTIC_NOTIFIC] =
	    iwl_rx_pm_debug_statistics_notif;
	priv->rx_handlers[BEACON_NOTIFICATION] = iwl_rx_beacon_notif;

	/*
	 * The same handler is used for both the REPLY to a discrete
	 * statistics request from the host as well as for the periodic
	 * statistics notifications (after received beacons) from the uCode.
	 */
	priv->rx_handlers[REPLY_STATISTICS_CMD] = iwl_reply_statistics;
	priv->rx_handlers[STATISTICS_NOTIFICATION] = iwl_rx_statistics;

	iwl_setup_rx_scan_handlers(priv);

	/* status change handler */
	priv->rx_handlers[CARD_STATE_NOTIFICATION] = iwl_rx_card_state_notif;

	priv->rx_handlers[MISSED_BEACONS_NOTIFICATION] =
	    iwl_rx_missed_beacon_notif;
	/* Rx handlers */
	priv->rx_handlers[REPLY_RX_PHY_CMD] = iwl_rx_reply_rx_phy;
	priv->rx_handlers[REPLY_RX_MPDU_CMD] = iwl_rx_reply_rx;
	/* block ack */
	priv->rx_handlers[REPLY_COMPRESSED_BA] = iwl_rx_reply_compressed_ba;
	/* Set up hardware specific Rx handlers */
	priv->cfg->ops->lib->rx_handler_setup(priv);
}

/**
 * iwl_rx_handle - Main entry function for receiving responses from uCode
 *
 * Uses the priv->rx_handlers callback function array to invoke
 * the appropriate handlers, including command responses,
 * frame-received notifications, and other notifications.
 */
void iwl_rx_handle(struct iwl_priv *priv)
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
		rxb = rxq->queue[i];

		/* If an RXB doesn't have a Rx queue slot associated with it,
		 * then a bug has been introduced in the queue refilling
		 * routines -- catch it here */
		BUG_ON(rxb == NULL);

		rxq->queue[i] = NULL;

		pci_unmap_page(priv->pci_dev, rxb->page_dma,
			       PAGE_SIZE << priv->hw_params.rx_page_order,
			       PCI_DMA_FROMDEVICE);
		pkt = rxb_addr(rxb);

		trace_iwlwifi_dev_rx(priv, pkt,
			le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK);

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
				iwl_rx_replenish_now(priv);
				count = 0;
			}
		}
	}

	/* Backtrack one entry */
	rxq->read = i;
	if (fill_rx)
		iwl_rx_replenish_now(priv);
	else
		iwl_rx_queue_restock(priv);
}

/* call this function to flush any scheduled tasklet */
static inline void iwl_synchronize_irq(struct iwl_priv *priv)
{
	/* wait to make sure we flush pending tasklet*/
	synchronize_irq(priv->pci_dev->irq);
	tasklet_kill(&priv->irq_tasklet);
}

static void iwl_irq_tasklet_legacy(struct iwl_priv *priv)
{
	u32 inta, handled = 0;
	u32 inta_fh;
	unsigned long flags;
	u32 i;
#ifdef CONFIG_IWLWIFI_DEBUG
	u32 inta_mask;
#endif

	spin_lock_irqsave(&priv->lock, flags);

	/* Ack/clear/reset pending uCode interrupts.
	 * Note:  Some bits in CSR_INT are "OR" of bits in CSR_FH_INT_STATUS,
	 *  and will clear only when CSR_FH_INT_STATUS gets cleared. */
	inta = iwl_read32(priv, CSR_INT);
	iwl_write32(priv, CSR_INT, inta);

	/* Ack/clear/reset pending flow-handler (DMA) interrupts.
	 * Any new interrupts that happen after this, either while we're
	 * in this tasklet, or later, will show up in next ISR/tasklet. */
	inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);
	iwl_write32(priv, CSR_FH_INT_STATUS, inta_fh);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_get_debug_level(priv) & IWL_DL_ISR) {
		/* just for debug */
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		IWL_DEBUG_ISR(priv, "inta 0x%08x, enabled 0x%08x, fh 0x%08x\n",
			      inta, inta_mask, inta_fh);
	}
#endif

	spin_unlock_irqrestore(&priv->lock, flags);

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
		priv->isr_stats.sw_err = inta;
		iwl_irq_handle_error(priv);
		handled |= CSR_INT_BIT_SW_ERR;
	}

	/*
	 * uCode wakes up after power-down sleep.
	 * Tell device about any new tx or host commands enqueued,
	 * and about any Rx buffers made available while asleep.
	 */
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
	if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX)) {
		iwl_rx_handle(priv);
		priv->isr_stats.rx++;
		handled |= (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX);
	}

	/* This "Tx" DMA channel is used only for loading uCode */
	if (inta & CSR_INT_BIT_FH_TX) {
		IWL_DEBUG_ISR(priv, "uCode load interrupt\n");
		priv->isr_stats.tx++;
		handled |= CSR_INT_BIT_FH_TX;
		/* Wake up uCode load routine, now that load is complete */
		priv->ucode_write_complete = 1;
		wake_up_interruptible(&priv->wait_command_queue);
	}

	if (inta & ~handled) {
		IWL_ERR(priv, "Unhandled INTA bits 0x%08x\n", inta & ~handled);
		priv->isr_stats.unhandled++;
	}

	if (inta & ~(priv->inta_mask)) {
		IWL_WARN(priv, "Disabled INTA bits 0x%08x were pending\n",
			 inta & ~priv->inta_mask);
		IWL_WARN(priv, "   with FH_INT = 0x%08x\n", inta_fh);
	}

	/* Re-enable all interrupts */
	/* only Re-enable if diabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl_enable_interrupts(priv);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_get_debug_level(priv) & (IWL_DL_ISR)) {
		inta = iwl_read32(priv, CSR_INT);
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);
		IWL_DEBUG_ISR(priv, "End inta 0x%08x, enabled 0x%08x, fh 0x%08x, "
			"flags 0x%08lx\n", inta, inta_mask, inta_fh, flags);
	}
#endif
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
	iwl_write32(priv, CSR_INT, priv->inta | ~priv->inta_mask);

	inta = priv->inta;

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_get_debug_level(priv) & IWL_DL_ISR) {
		/* just for debug */
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		IWL_DEBUG_ISR(priv, "inta 0x%08x, enabled 0x%08x\n ",
				inta, inta_mask);
	}
#endif

	spin_unlock_irqrestore(&priv->lock, flags);

	/* saved interrupt in inta variable now we can reset priv->inta */
	priv->inta = 0;

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
		priv->isr_stats.sw_err = inta;
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
					CSR49_FH_INT_RX_MASK);
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
		iwl_write32(priv, CSR_FH_INT_STATUS, CSR49_FH_INT_TX_MASK);
		IWL_DEBUG_ISR(priv, "uCode load interrupt\n");
		priv->isr_stats.tx++;
		handled |= CSR_INT_BIT_FH_TX;
		/* Wake up uCode load routine, now that load is complete */
		priv->ucode_write_complete = 1;
		wake_up_interruptible(&priv->wait_command_queue);
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
	/* only Re-enable if diabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl_enable_interrupts(priv);
}


/******************************************************************************
 *
 * uCode download functions
 *
 ******************************************************************************/

static void iwl_dealloc_ucode_pci(struct iwl_priv *priv)
{
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_code);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_data);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_data_backup);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_init);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_init_data);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_boot);
}

static void iwl_nic_start(struct iwl_priv *priv)
{
	/* Remove all resets to allow NIC to operate */
	iwl_write32(priv, CSR_RESET, 0);
}


static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context);
static int iwl_mac_setup_register(struct iwl_priv *priv);

static int __must_check iwl_request_firmware(struct iwl_priv *priv, bool first)
{
	const char *name_pre = priv->cfg->fw_name_pre;

	if (first)
		priv->fw_index = priv->cfg->ucode_api_max;
	else
		priv->fw_index--;

	if (priv->fw_index < priv->cfg->ucode_api_min) {
		IWL_ERR(priv, "no suitable firmware found!\n");
		return -ENOENT;
	}

	sprintf(priv->firmware_name, "%s%d%s",
		name_pre, priv->fw_index, ".ucode");

	IWL_DEBUG_INFO(priv, "attempting to load firmware '%s'\n",
		       priv->firmware_name);

	return request_firmware_nowait(THIS_MODULE, 1, priv->firmware_name,
				       &priv->pci_dev->dev, GFP_KERNEL, priv,
				       iwl_ucode_callback);
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
	const unsigned int api_max = priv->cfg->ucode_api_max;
	const unsigned int api_min = priv->cfg->ucode_api_min;
	u8 *src;
	size_t len;
	u32 api_ver, build;
	u32 inst_size, data_size, init_size, init_data_size, boot_size;
	int err;
	u16 eeprom_ver;

	if (!ucode_raw) {
		IWL_ERR(priv, "request for firmware file '%s' failed.\n",
			priv->firmware_name);
		goto try_again;
	}

	IWL_DEBUG_INFO(priv, "Loaded firmware file '%s' (%zd bytes).\n",
		       priv->firmware_name, ucode_raw->size);

	/* Make sure that we got at least the v1 header! */
	if (ucode_raw->size < priv->cfg->ops->ucode->get_header_size(1)) {
		IWL_ERR(priv, "File size way too small!\n");
		goto try_again;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (struct iwl_ucode_header *)ucode_raw->data;

	priv->ucode_ver = le32_to_cpu(ucode->ver);
	api_ver = IWL_UCODE_API(priv->ucode_ver);
	build = priv->cfg->ops->ucode->get_build(ucode, api_ver);
	inst_size = priv->cfg->ops->ucode->get_inst_size(ucode, api_ver);
	data_size = priv->cfg->ops->ucode->get_data_size(ucode, api_ver);
	init_size = priv->cfg->ops->ucode->get_init_size(ucode, api_ver);
	init_data_size =
		priv->cfg->ops->ucode->get_init_data_size(ucode, api_ver);
	boot_size = priv->cfg->ops->ucode->get_boot_size(ucode, api_ver);
	src = priv->cfg->ops->ucode->get_data(ucode, api_ver);

	/* api_ver should match the api version forming part of the
	 * firmware filename ... but we don't check for that and only rely
	 * on the API version read from firmware header from here on forward */

	if (api_ver < api_min || api_ver > api_max) {
		IWL_ERR(priv, "Driver unable to support your firmware API. "
			  "Driver supports v%u, firmware is v%u.\n",
			  api_max, api_ver);
		goto try_again;
	}

	if (api_ver != api_max)
		IWL_ERR(priv, "Firmware has old API version. Expected v%u, "
			  "got v%u. New firmware can be obtained "
			  "from http://www.intellinuxwireless.org.\n",
			  api_max, api_ver);

	IWL_INFO(priv, "loaded firmware version %u.%u.%u.%u\n",
	       IWL_UCODE_MAJOR(priv->ucode_ver),
	       IWL_UCODE_MINOR(priv->ucode_ver),
	       IWL_UCODE_API(priv->ucode_ver),
	       IWL_UCODE_SERIAL(priv->ucode_ver));

	snprintf(priv->hw->wiphy->fw_version,
		 sizeof(priv->hw->wiphy->fw_version),
		 "%u.%u.%u.%u",
		 IWL_UCODE_MAJOR(priv->ucode_ver),
		 IWL_UCODE_MINOR(priv->ucode_ver),
		 IWL_UCODE_API(priv->ucode_ver),
		 IWL_UCODE_SERIAL(priv->ucode_ver));

	if (build)
		IWL_DEBUG_INFO(priv, "Build %u\n", build);

	eeprom_ver = iwl_eeprom_query16(priv, EEPROM_VERSION);
	IWL_DEBUG_INFO(priv, "NVM Type: %s, version: 0x%x\n",
		       (priv->nvm_device_type == NVM_DEVICE_TYPE_OTP)
		       ? "OTP" : "EEPROM", eeprom_ver);

	IWL_DEBUG_INFO(priv, "f/w package hdr ucode version raw = 0x%x\n",
		       priv->ucode_ver);
	IWL_DEBUG_INFO(priv, "f/w package hdr runtime inst size = %u\n",
		       inst_size);
	IWL_DEBUG_INFO(priv, "f/w package hdr runtime data size = %u\n",
		       data_size);
	IWL_DEBUG_INFO(priv, "f/w package hdr init inst size = %u\n",
		       init_size);
	IWL_DEBUG_INFO(priv, "f/w package hdr init data size = %u\n",
		       init_data_size);
	IWL_DEBUG_INFO(priv, "f/w package hdr boot inst size = %u\n",
		       boot_size);

	/*
	 * For any of the failures below (before allocating pci memory)
	 * we will try to load a version with a smaller API -- maybe the
	 * user just got a corrupted version of the latest API.
	 */

	/* Verify size of file vs. image size info in file's header */
	if (ucode_raw->size !=
		priv->cfg->ops->ucode->get_header_size(api_ver) +
		inst_size + data_size + init_size +
		init_data_size + boot_size) {

		IWL_DEBUG_INFO(priv,
			"uCode file size %d does not match expected size\n",
			(int)ucode_raw->size);
		goto try_again;
	}

	/* Verify that uCode images will fit in card's SRAM */
	if (inst_size > priv->hw_params.max_inst_size) {
		IWL_DEBUG_INFO(priv, "uCode instr len %d too large to fit in\n",
			       inst_size);
		goto try_again;
	}

	if (data_size > priv->hw_params.max_data_size) {
		IWL_DEBUG_INFO(priv, "uCode data len %d too large to fit in\n",
				data_size);
		goto try_again;
	}
	if (init_size > priv->hw_params.max_inst_size) {
		IWL_INFO(priv, "uCode init instr len %d too large to fit in\n",
			init_size);
		goto try_again;
	}
	if (init_data_size > priv->hw_params.max_data_size) {
		IWL_INFO(priv, "uCode init data len %d too large to fit in\n",
		      init_data_size);
		goto try_again;
	}
	if (boot_size > priv->hw_params.max_bsm_size) {
		IWL_INFO(priv, "uCode boot instr len %d too large to fit in\n",
			boot_size);
		goto try_again;
	}

	/* Allocate ucode buffers for card's bus-master loading ... */

	/* Runtime instructions and 2 copies of data:
	 * 1) unmodified from disk
	 * 2) backup cache for save/restore during power-downs */
	priv->ucode_code.len = inst_size;
	iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_code);

	priv->ucode_data.len = data_size;
	iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_data);

	priv->ucode_data_backup.len = data_size;
	iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_data_backup);

	if (!priv->ucode_code.v_addr || !priv->ucode_data.v_addr ||
	    !priv->ucode_data_backup.v_addr)
		goto err_pci_alloc;

	/* Initialization instructions and data */
	if (init_size && init_data_size) {
		priv->ucode_init.len = init_size;
		iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_init);

		priv->ucode_init_data.len = init_data_size;
		iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_init_data);

		if (!priv->ucode_init.v_addr || !priv->ucode_init_data.v_addr)
			goto err_pci_alloc;
	}

	/* Bootstrap (instructions only, no data) */
	if (boot_size) {
		priv->ucode_boot.len = boot_size;
		iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_boot);

		if (!priv->ucode_boot.v_addr)
			goto err_pci_alloc;
	}

	/* Copy images into buffers for card's bus-master reads ... */

	/* Runtime instructions (first block of data in file) */
	len = inst_size;
	IWL_DEBUG_INFO(priv, "Copying (but not loading) uCode instr len %Zd\n", len);
	memcpy(priv->ucode_code.v_addr, src, len);
	src += len;

	IWL_DEBUG_INFO(priv, "uCode instr buf vaddr = 0x%p, paddr = 0x%08x\n",
		priv->ucode_code.v_addr, (u32)priv->ucode_code.p_addr);

	/* Runtime data (2nd block)
	 * NOTE:  Copy into backup buffer will be done in iwl_up()  */
	len = data_size;
	IWL_DEBUG_INFO(priv, "Copying (but not loading) uCode data len %Zd\n", len);
	memcpy(priv->ucode_data.v_addr, src, len);
	memcpy(priv->ucode_data_backup.v_addr, src, len);
	src += len;

	/* Initialization instructions (3rd block) */
	if (init_size) {
		len = init_size;
		IWL_DEBUG_INFO(priv, "Copying (but not loading) init instr len %Zd\n",
				len);
		memcpy(priv->ucode_init.v_addr, src, len);
		src += len;
	}

	/* Initialization data (4th block) */
	if (init_data_size) {
		len = init_data_size;
		IWL_DEBUG_INFO(priv, "Copying (but not loading) init data len %Zd\n",
			       len);
		memcpy(priv->ucode_init_data.v_addr, src, len);
		src += len;
	}

	/* Bootstrap instructions (5th block) */
	len = boot_size;
	IWL_DEBUG_INFO(priv, "Copying (but not loading) boot instr len %Zd\n", len);
	memcpy(priv->ucode_boot.v_addr, src, len);

	/**************************************************
	 * This is still part of probe() in a sense...
	 *
	 * 9. Setup and register with mac80211 and debugfs
	 **************************************************/
	err = iwl_mac_setup_register(priv);
	if (err)
		goto out_unbind;

	err = iwl_dbgfs_register(priv, DRV_NAME);
	if (err)
		IWL_ERR(priv, "failed to create debugfs files. Ignoring error: %d\n", err);

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);
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
	"ADVANCED SYSASSERT"
};

static const char *desc_lookup(int i)
{
	int max = ARRAY_SIZE(desc_lookup_text) - 1;

	if (i < 0 || i > max)
		i = max;

	return desc_lookup_text[i];
}

#define ERROR_START_OFFSET  (1 * sizeof(u32))
#define ERROR_ELEM_SIZE     (7 * sizeof(u32))

void iwl_dump_nic_error_log(struct iwl_priv *priv)
{
	u32 data2, line;
	u32 desc, time, count, base, data1;
	u32 blink1, blink2, ilink1, ilink2;

	if (priv->ucode_type == UCODE_INIT)
		base = le32_to_cpu(priv->card_alive_init.error_event_table_ptr);
	else
		base = le32_to_cpu(priv->card_alive.error_event_table_ptr);

	if (!priv->cfg->ops->lib->is_valid_rtc_data_addr(base)) {
		IWL_ERR(priv,
			"Not valid error log pointer 0x%08X for %s uCode\n",
			base, (priv->ucode_type == UCODE_INIT) ? "Init" : "RT");
		return;
	}

	count = iwl_read_targ_mem(priv, base);

	if (ERROR_START_OFFSET <= count * ERROR_ELEM_SIZE) {
		IWL_ERR(priv, "Start IWL Error Log Dump:\n");
		IWL_ERR(priv, "Status: 0x%08lX, count: %d\n",
			priv->status, count);
	}

	desc = iwl_read_targ_mem(priv, base + 1 * sizeof(u32));
	blink1 = iwl_read_targ_mem(priv, base + 3 * sizeof(u32));
	blink2 = iwl_read_targ_mem(priv, base + 4 * sizeof(u32));
	ilink1 = iwl_read_targ_mem(priv, base + 5 * sizeof(u32));
	ilink2 = iwl_read_targ_mem(priv, base + 6 * sizeof(u32));
	data1 = iwl_read_targ_mem(priv, base + 7 * sizeof(u32));
	data2 = iwl_read_targ_mem(priv, base + 8 * sizeof(u32));
	line = iwl_read_targ_mem(priv, base + 9 * sizeof(u32));
	time = iwl_read_targ_mem(priv, base + 11 * sizeof(u32));

	trace_iwlwifi_dev_ucode_error(priv, desc, time, data1, data2, line,
				      blink1, blink2, ilink1, ilink2);

	IWL_ERR(priv, "Desc                               Time       "
		"data1      data2      line\n");
	IWL_ERR(priv, "%-28s (#%02d) %010u 0x%08X 0x%08X %u\n",
		desc_lookup(desc), desc, time, data1, data2, line);
	IWL_ERR(priv, "blink1  blink2  ilink1  ilink2\n");
	IWL_ERR(priv, "0x%05X 0x%05X 0x%05X 0x%05X\n", blink1, blink2,
		ilink1, ilink2);

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
	if (priv->ucode_type == UCODE_INIT)
		base = le32_to_cpu(priv->card_alive_init.log_event_table_ptr);
	else
		base = le32_to_cpu(priv->card_alive.log_event_table_ptr);

	if (mode == 0)
		event_size = 2 * sizeof(u32);
	else
		event_size = 3 * sizeof(u32);

	ptr = base + EVENT_START_OFFSET + (start_idx * event_size);

	/* Make sure device is powered up for SRAM reads */
	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	iwl_grab_nic_access(priv);

	/* Set starting address; reads will auto-increment */
	_iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR, ptr);
	rmb();

	/* "time" is actually "data" for mode 0 (no timestamp).
	* place event id # at far right for easier visual parsing. */
	for (i = 0; i < num_events; i++) {
		ev = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		time = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
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
			data = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
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

/* For sanity check only.  Actual size is determined by uCode, typ. 512 */
#define MAX_EVENT_LOG_SIZE (512)

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
	int pos = 0;
	size_t bufsz = 0;

	if (priv->ucode_type == UCODE_INIT)
		base = le32_to_cpu(priv->card_alive_init.log_event_table_ptr);
	else
		base = le32_to_cpu(priv->card_alive.log_event_table_ptr);

	if (!priv->cfg->ops->lib->is_valid_rtc_data_addr(base)) {
		IWL_ERR(priv,
			"Invalid event log pointer 0x%08X for %s uCode\n",
			base, (priv->ucode_type == UCODE_INIT) ? "Init" : "RT");
		return -EINVAL;
	}

	/* event log header */
	capacity = iwl_read_targ_mem(priv, base);
	mode = iwl_read_targ_mem(priv, base + (1 * sizeof(u32)));
	num_wraps = iwl_read_targ_mem(priv, base + (2 * sizeof(u32)));
	next_entry = iwl_read_targ_mem(priv, base + (3 * sizeof(u32)));

	if (capacity > MAX_EVENT_LOG_SIZE) {
		IWL_ERR(priv, "Log capacity %d is bogus, limit to %d entries\n",
			capacity, MAX_EVENT_LOG_SIZE);
		capacity = MAX_EVENT_LOG_SIZE;
	}

	if (next_entry > MAX_EVENT_LOG_SIZE) {
		IWL_ERR(priv, "Log write index %d is bogus, limit to %d\n",
			next_entry, MAX_EVENT_LOG_SIZE);
		next_entry = MAX_EVENT_LOG_SIZE;
	}

	size = num_wraps ? capacity : next_entry;

	/* bail out if nothing in log */
	if (size == 0) {
		IWL_ERR(priv, "Start IWL Event Log Dump: nothing in log\n");
		return pos;
	}

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

/**
 * iwl_alive_start - called after REPLY_ALIVE notification received
 *                   from protocol/runtime uCode (initialization uCode's
 *                   Alive gets handled by iwl_init_alive_start()).
 */
static void iwl_alive_start(struct iwl_priv *priv)
{
	int ret = 0;

	IWL_DEBUG_INFO(priv, "Runtime Alive received.\n");

	if (priv->card_alive.is_valid != UCODE_VALID_OK) {
		/* We had an error bringing up the hardware, so take it
		 * all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Alive failed.\n");
		goto restart;
	}

	/* Initialize uCode has loaded Runtime uCode ... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "runtime" alive if code weren't properly loaded.  */
	if (iwl_verify_ucode(priv)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Bad runtime uCode load.\n");
		goto restart;
	}

	iwl_clear_stations_table(priv);
	ret = priv->cfg->ops->lib->alive_notify(priv);
	if (ret) {
		IWL_WARN(priv,
			"Could not complete ALIVE transition [ntf]: %d\n", ret);
		goto restart;
	}

	/* After the ALIVE response, we can send host commands to the uCode */
	set_bit(STATUS_ALIVE, &priv->status);

	if (iwl_is_rfkill(priv))
		return;

	ieee80211_wake_queues(priv->hw);

	priv->active_rate = priv->rates_mask;
	priv->active_rate_basic = priv->rates_mask & IWL_BASIC_RATES_MASK;

	/* Configure Tx antenna selection based on H/W config */
	if (priv->cfg->ops->hcmd->set_tx_ant)
		priv->cfg->ops->hcmd->set_tx_ant(priv, priv->cfg->valid_tx_ant);

	if (iwl_is_associated(priv)) {
		struct iwl_rxon_cmd *active_rxon =
				(struct iwl_rxon_cmd *)&priv->active_rxon;
		/* apply any changes in staging */
		priv->staging_rxon.filter_flags |= RXON_FILTER_ASSOC_MSK;
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	} else {
		/* Initialize our rx_config data */
		iwl_connection_init_rx_config(priv, priv->iw_mode);

		if (priv->cfg->ops->hcmd->set_rxon_chain)
			priv->cfg->ops->hcmd->set_rxon_chain(priv);

		memcpy(priv->staging_rxon.node_addr, priv->mac_addr, ETH_ALEN);
	}

	/* Configure Bluetooth device coexistence support */
	iwl_send_bt_config(priv);

	iwl_reset_run_time_calib(priv);

	/* Configure the adapter for unassociated operation */
	iwlcore_commit_rxon(priv);

	/* At this point, the NIC is initialized and operational */
	iwl_rf_kill_ct_config(priv);

	iwl_leds_init(priv);

	IWL_DEBUG_INFO(priv, "ALIVE processing complete.\n");
	set_bit(STATUS_READY, &priv->status);
	wake_up_interruptible(&priv->wait_command_queue);

	iwl_power_update_mode(priv, true);

	/* reassociate for ADHOC mode */
	if (priv->vif && (priv->iw_mode == NL80211_IFTYPE_ADHOC)) {
		struct sk_buff *beacon = ieee80211_beacon_get(priv->hw,
								priv->vif);
		if (beacon)
			iwl_mac_beacon_update(priv->hw, beacon);
	}


	if (test_and_clear_bit(STATUS_MODE_PENDING, &priv->status))
		iwl_set_mode(priv, priv->iw_mode);

	return;

 restart:
	queue_work(priv->workqueue, &priv->restart);
}

static void iwl_cancel_deferred_work(struct iwl_priv *priv);

static void __iwl_down(struct iwl_priv *priv)
{
	unsigned long flags;
	int exit_pending = test_bit(STATUS_EXIT_PENDING, &priv->status);

	IWL_DEBUG_INFO(priv, DRV_NAME " is going down\n");

	if (!exit_pending)
		set_bit(STATUS_EXIT_PENDING, &priv->status);

	iwl_clear_stations_table(priv);

	/* Unblock any waiting calls */
	wake_up_interruptible_all(&priv->wait_command_queue);

	/* Wipe out the EXIT_PENDING status bit if we are not actually
	 * exiting the module */
	if (!exit_pending)
		clear_bit(STATUS_EXIT_PENDING, &priv->status);

	/* stop and reset the on-board processor */
	iwl_write32(priv, CSR_RESET, CSR_RESET_REG_FLAG_NEVO_RESET);

	/* tell the device to stop sending interrupts */
	spin_lock_irqsave(&priv->lock, flags);
	iwl_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
	iwl_synchronize_irq(priv);

	if (priv->mac80211_registered)
		ieee80211_stop_queues(priv->hw);

	/* If we have not previously called iwl_init() then
	 * clear all bits but the RF Kill bit and return */
	if (!iwl_is_init(priv)) {
		priv->status = test_bit(STATUS_RF_KILL_HW, &priv->status) <<
					STATUS_RF_KILL_HW |
			       test_bit(STATUS_GEO_CONFIGURED, &priv->status) <<
					STATUS_GEO_CONFIGURED |
			       test_bit(STATUS_EXIT_PENDING, &priv->status) <<
					STATUS_EXIT_PENDING;
		goto exit;
	}

	/* ...otherwise clear out all the status bits but the RF Kill
	 * bit and continue taking the NIC down. */
	priv->status &= test_bit(STATUS_RF_KILL_HW, &priv->status) <<
				STATUS_RF_KILL_HW |
			test_bit(STATUS_GEO_CONFIGURED, &priv->status) <<
				STATUS_GEO_CONFIGURED |
			test_bit(STATUS_FW_ERROR, &priv->status) <<
				STATUS_FW_ERROR |
		       test_bit(STATUS_EXIT_PENDING, &priv->status) <<
				STATUS_EXIT_PENDING;

	/* device going down, Stop using ICT table */
	iwl_disable_ict(priv);

	iwl_txq_ctx_stop(priv);
	iwl_rxq_stop(priv);

	/* Power-down device's busmaster DMA clocks */
	iwl_write_prph(priv, APMG_CLK_DIS_REG, APMG_CLK_VAL_DMA_CLK_RQT);
	udelay(5);

	/* Make sure (redundant) we've released our request to stay awake */
	iwl_clear_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	/* Stop the device, and put it in low power state */
	priv->cfg->ops->lib->apm_ops.stop(priv);

 exit:
	memset(&priv->card_alive, 0, sizeof(struct iwl_alive_resp));

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);
	priv->ibss_beacon = NULL;

	/* clear out any free frames */
	iwl_clear_free_frames(priv);
}

static void iwl_down(struct iwl_priv *priv)
{
	mutex_lock(&priv->mutex);
	__iwl_down(priv);
	mutex_unlock(&priv->mutex);

	iwl_cancel_deferred_work(priv);
}

#define HW_READY_TIMEOUT (50)

static int iwl_set_hw_ready(struct iwl_priv *priv)
{
	int ret = 0;

	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
		CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	/* See if we got it */
	ret = iwl_poll_bit(priv, CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
				CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
				HW_READY_TIMEOUT);
	if (ret != -ETIMEDOUT)
		priv->hw_ready = true;
	else
		priv->hw_ready = false;

	IWL_DEBUG_INFO(priv, "hardware %s\n",
		      (priv->hw_ready == 1) ? "ready" : "not ready");
	return ret;
}

static int iwl_prepare_card_hw(struct iwl_priv *priv)
{
	int ret = 0;

	IWL_DEBUG_INFO(priv, "iwl_prepare_card_hw enter \n");

	ret = iwl_set_hw_ready(priv);
	if (priv->hw_ready)
		return ret;

	/* If HW is not ready, prepare the conditions to check again */
	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			CSR_HW_IF_CONFIG_REG_PREPARE);

	ret = iwl_poll_bit(priv, CSR_HW_IF_CONFIG_REG,
			~CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE,
			CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE, 150000);

	/* HW should be ready by now, check again. */
	if (ret != -ETIMEDOUT)
		iwl_set_hw_ready(priv);

	return ret;
}

#define MAX_HW_RESTARTS 5

static int __iwl_up(struct iwl_priv *priv)
{
	int i;
	int ret;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_WARN(priv, "Exit pending; will not bring the NIC up\n");
		return -EIO;
	}

	if (!priv->ucode_data_backup.v_addr || !priv->ucode_data.v_addr) {
		IWL_ERR(priv, "ucode not available for device bringup\n");
		return -EIO;
	}

	iwl_prepare_card_hw(priv);

	if (!priv->hw_ready) {
		IWL_WARN(priv, "Exit HW not ready\n");
		return -EIO;
	}

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (iwl_read32(priv, CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		set_bit(STATUS_RF_KILL_HW, &priv->status);

	if (iwl_is_rfkill(priv)) {
		wiphy_rfkill_set_hw_state(priv->hw->wiphy, true);

		iwl_enable_interrupts(priv);
		IWL_WARN(priv, "Radio disabled by HW RF Kill switch\n");
		return 0;
	}

	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);

	ret = iwl_hw_nic_init(priv);
	if (ret) {
		IWL_ERR(priv, "Unable to init nic\n");
		return ret;
	}

	/* make sure rfkill handshake bits are cleared */
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable host interrupts */
	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);
	iwl_enable_interrupts(priv);

	/* really make sure rfkill handshake bits are cleared */
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);

	/* Copy original ucode data image from disk into backup cache.
	 * This will be used to initialize the on-board processor's
	 * data SRAM for a clean start when the runtime program first loads. */
	memcpy(priv->ucode_data_backup.v_addr, priv->ucode_data.v_addr,
	       priv->ucode_data.len);

	for (i = 0; i < MAX_HW_RESTARTS; i++) {

		iwl_clear_stations_table(priv);

		/* load bootstrap state machine,
		 * load bootstrap program into processor's memory,
		 * prepare to load the "initialize" uCode */
		ret = priv->cfg->ops->lib->load_ucode(priv);

		if (ret) {
			IWL_ERR(priv, "Unable to set up bootstrap uCode: %d\n",
				ret);
			continue;
		}

		/* start card; "initialize" will load runtime ucode */
		iwl_nic_start(priv);

		IWL_DEBUG_INFO(priv, DRV_NAME " is coming up\n");

		return 0;
	}

	set_bit(STATUS_EXIT_PENDING, &priv->status);
	__iwl_down(priv);
	clear_bit(STATUS_EXIT_PENDING, &priv->status);

	/* tried to restart and config the device for as long as our
	 * patience could withstand */
	IWL_ERR(priv, "Unable to initialize device after %d attempts.\n", i);
	return -EIO;
}


/*****************************************************************************
 *
 * Workqueue callbacks
 *
 *****************************************************************************/

static void iwl_bg_init_alive_start(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, init_alive_start.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	priv->cfg->ops->lib->init_alive_start(priv);
	mutex_unlock(&priv->mutex);
}

static void iwl_bg_alive_start(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, alive_start.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* enable dram interrupt */
	iwl_reset_ict(priv);

	mutex_lock(&priv->mutex);
	iwl_alive_start(priv);
	mutex_unlock(&priv->mutex);
}

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
		iwl_chain_noise_calibration(priv, &priv->statistics);

		iwl_sensitivity_calibration(priv, &priv->statistics);
	}

	mutex_unlock(&priv->mutex);
	return;
}

static void iwl_bg_restart(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, restart);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (test_and_clear_bit(STATUS_FW_ERROR, &priv->status)) {
		mutex_lock(&priv->mutex);
		priv->vif = NULL;
		priv->is_open = 0;
		mutex_unlock(&priv->mutex);
		iwl_down(priv);
		ieee80211_restart_hw(priv->hw);
	} else {
		iwl_down(priv);

		if (test_bit(STATUS_EXIT_PENDING, &priv->status))
			return;

		mutex_lock(&priv->mutex);
		__iwl_up(priv);
		mutex_unlock(&priv->mutex);
	}
}

static void iwl_bg_rx_replenish(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, rx_replenish);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwl_rx_replenish(priv);
	mutex_unlock(&priv->mutex);
}

#define IWL_DELAY_NEXT_SCAN (HZ*2)

void iwl_post_associate(struct iwl_priv *priv)
{
	struct ieee80211_conf *conf = NULL;
	int ret = 0;
	unsigned long flags;

	if (priv->iw_mode == NL80211_IFTYPE_AP) {
		IWL_ERR(priv, "%s Should not be called in AP mode\n", __func__);
		return;
	}

	IWL_DEBUG_ASSOC(priv, "Associated as %d to: %pM\n",
			priv->assoc_id, priv->active_rxon.bssid_addr);


	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;


	if (!priv->vif || !priv->is_open)
		return;

	iwl_scan_cancel_timeout(priv, 200);

	conf = ieee80211_get_hw_conf(priv->hw);

	priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwlcore_commit_rxon(priv);

	iwl_setup_rxon_timing(priv);
	ret = iwl_send_cmd_pdu(priv, REPLY_RXON_TIMING,
			      sizeof(priv->rxon_timing), &priv->rxon_timing);
	if (ret)
		IWL_WARN(priv, "REPLY_RXON_TIMING failed - "
			    "Attempting to continue.\n");

	priv->staging_rxon.filter_flags |= RXON_FILTER_ASSOC_MSK;

	iwl_set_rxon_ht(priv, &priv->current_ht_config);

	if (priv->cfg->ops->hcmd->set_rxon_chain)
		priv->cfg->ops->hcmd->set_rxon_chain(priv);

	priv->staging_rxon.assoc_id = cpu_to_le16(priv->assoc_id);

	IWL_DEBUG_ASSOC(priv, "assoc id %d beacon interval %d\n",
			priv->assoc_id, priv->beacon_int);

	if (priv->assoc_capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		priv->staging_rxon.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		priv->staging_rxon.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;

	if (priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK) {
		if (priv->assoc_capability & WLAN_CAPABILITY_SHORT_SLOT_TIME)
			priv->staging_rxon.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		if (priv->iw_mode == NL80211_IFTYPE_ADHOC)
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

	}

	iwlcore_commit_rxon(priv);

	switch (priv->iw_mode) {
	case NL80211_IFTYPE_STATION:
		break;

	case NL80211_IFTYPE_ADHOC:

		/* assume default assoc id */
		priv->assoc_id = 1;

		iwl_rxon_add_station(priv, priv->bssid, 0);
		iwl_send_beacon_cmd(priv);

		break;

	default:
		IWL_ERR(priv, "%s Should not be called in %d mode\n",
			  __func__, priv->iw_mode);
		break;
	}

	if (priv->iw_mode == NL80211_IFTYPE_ADHOC)
		priv->assoc_station_added = 1;

	spin_lock_irqsave(&priv->lock, flags);
	iwl_activate_qos(priv, 0);
	spin_unlock_irqrestore(&priv->lock, flags);

	/* the chain noise calibration will enabled PM upon completion
	 * If chain noise has already been run, then we need to enable
	 * power management here */
	if (priv->chain_noise_data.state == IWL_CHAIN_NOISE_DONE)
		iwl_power_update_mode(priv, false);

	/* Enable Rx differential gain and sensitivity calibrations */
	iwl_chain_noise_reset(priv);
	priv->start_calib = 1;

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
static int iwl_mac_setup_register(struct iwl_priv *priv)
{
	int ret;
	struct ieee80211_hw *hw = priv->hw;
	hw->rate_control_algorithm = "iwl-agn-rs";

	/* Tell mac80211 our characteristics */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_NOISE_DBM |
		    IEEE80211_HW_AMPDU_AGGREGATION |
		    IEEE80211_HW_SPECTRUM_MGMT;

	if (!priv->cfg->broken_powersave)
		hw->flags |= IEEE80211_HW_SUPPORTS_PS |
			     IEEE80211_HW_SUPPORTS_DYNAMIC_PS;

	if (priv->cfg->sku & IWL_SKU_N)
		hw->flags |= IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS |
			     IEEE80211_HW_SUPPORTS_STATIC_SMPS;

	hw->sta_data_size = sizeof(struct iwl_station_priv);
	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY |
			    WIPHY_FLAG_DISABLE_BEACON_HINTS;

	/*
	 * For now, disable PS by default because it affects
	 * RX performance significantly.
	 */
	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->max_scan_ssids = PROBE_OPTION_MAX;
	/* we create the 802.11 header and a zero-length SSID element */
	hw->wiphy->max_scan_ie_len = IWL_MAX_PROBE_REQUEST - 24 - 2;

	/* Default value; 4 EDCA QOS priorities */
	hw->queues = 4;

	hw->max_listen_interval = IWL_CONN_MAX_LISTEN_INTERVAL;

	if (priv->bands[IEEE80211_BAND_2GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&priv->bands[IEEE80211_BAND_2GHZ];
	if (priv->bands[IEEE80211_BAND_5GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&priv->bands[IEEE80211_BAND_5GHZ];

	ret = ieee80211_register_hw(priv->hw);
	if (ret) {
		IWL_ERR(priv, "Failed to register hw (error %d)\n", ret);
		return ret;
	}
	priv->mac80211_registered = 1;

	return 0;
}


static int iwl_mac_start(struct ieee80211_hw *hw)
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

	if (iwl_is_rfkill(priv))
		goto out;

	IWL_DEBUG_INFO(priv, "Start UP work done.\n");

	/* Wait for START_ALIVE from Run Time ucode. Otherwise callbacks from
	 * mac80211 will not be run successfully. */
	ret = wait_event_interruptible_timeout(priv->wait_command_queue,
			test_bit(STATUS_READY, &priv->status),
			UCODE_READY_TIMEOUT);
	if (!ret) {
		if (!test_bit(STATUS_READY, &priv->status)) {
			IWL_ERR(priv, "START_ALIVE timeout after %dms.\n",
				jiffies_to_msecs(UCODE_READY_TIMEOUT));
			return -ETIMEDOUT;
		}
	}

	iwl_led_start(priv);

out:
	priv->is_open = 1;
	IWL_DEBUG_MAC80211(priv, "leave\n");
	return 0;
}

static void iwl_mac_stop(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!priv->is_open)
		return;

	priv->is_open = 0;

	if (iwl_is_ready_rf(priv) || test_bit(STATUS_SCAN_HW, &priv->status)) {
		/* stop mac, cancel any scan request and clear
		 * RXON_FILTER_ASSOC_MSK BIT
		 */
		mutex_lock(&priv->mutex);
		iwl_scan_cancel_timeout(priv, 100);
		mutex_unlock(&priv->mutex);
	}

	iwl_down(priv);

	flush_workqueue(priv->workqueue);

	/* enable interrupts again in order to receive rfkill changes */
	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);
	iwl_enable_interrupts(priv);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static int iwl_mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MACDUMP(priv, "enter\n");

	IWL_DEBUG_TX(priv, "dev->xmit(%d bytes) at rate 0x%02x\n", skb->len,
		     ieee80211_get_tx_rate(hw, IEEE80211_SKB_CB(skb))->bitrate);

	if (iwl_tx_skb(priv, skb))
		dev_kfree_skb_any(skb);

	IWL_DEBUG_MACDUMP(priv, "leave\n");
	return NETDEV_TX_OK;
}

void iwl_config_ap(struct iwl_priv *priv)
{
	int ret = 0;
	unsigned long flags;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* The following should be done only at AP bring up */
	if (!iwl_is_associated(priv)) {

		/* RXON - unassoc (to set timing command) */
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwlcore_commit_rxon(priv);

		/* RXON Timing */
		iwl_setup_rxon_timing(priv);
		ret = iwl_send_cmd_pdu(priv, REPLY_RXON_TIMING,
				sizeof(priv->rxon_timing), &priv->rxon_timing);
		if (ret)
			IWL_WARN(priv, "REPLY_RXON_TIMING failed - "
					"Attempting to continue.\n");

		/* AP has all antennas */
		priv->chain_noise_data.active_chains =
			priv->hw_params.valid_rx_ant;
		iwl_set_rxon_ht(priv, &priv->current_ht_config);
		if (priv->cfg->ops->hcmd->set_rxon_chain)
			priv->cfg->ops->hcmd->set_rxon_chain(priv);

		/* FIXME: what should be the assoc_id for AP? */
		priv->staging_rxon.assoc_id = cpu_to_le16(priv->assoc_id);
		if (priv->assoc_capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
			priv->staging_rxon.flags |=
				RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			priv->staging_rxon.flags &=
				~RXON_FLG_SHORT_PREAMBLE_MSK;

		if (priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK) {
			if (priv->assoc_capability &
				WLAN_CAPABILITY_SHORT_SLOT_TIME)
				priv->staging_rxon.flags |=
					RXON_FLG_SHORT_SLOT_MSK;
			else
				priv->staging_rxon.flags &=
					~RXON_FLG_SHORT_SLOT_MSK;

			if (priv->iw_mode == NL80211_IFTYPE_ADHOC)
				priv->staging_rxon.flags &=
					~RXON_FLG_SHORT_SLOT_MSK;
		}
		/* restore RXON assoc */
		priv->staging_rxon.filter_flags |= RXON_FILTER_ASSOC_MSK;
		iwlcore_commit_rxon(priv);
		iwl_reset_qos(priv);
		spin_lock_irqsave(&priv->lock, flags);
		iwl_activate_qos(priv, 1);
		spin_unlock_irqrestore(&priv->lock, flags);
		iwl_add_bcast_station(priv);
	}
	iwl_send_beacon_cmd(priv);

	/* FIXME - we need to add code here to detect a totally new
	 * configuration, reset the AP, unassoc, rxon timing, assoc,
	 * clear sta table, add BCAST sta... */
}

static void iwl_mac_update_tkip_key(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_key_conf *keyconf,
				    struct ieee80211_sta *sta,
				    u32 iv32, u16 *phase1key)
{

	struct iwl_priv *priv = hw->priv;
	IWL_DEBUG_MAC80211(priv, "enter\n");

	iwl_update_tkip_key(priv, keyconf,
			    sta ? sta->addr : iwl_bcast_addr,
			    iv32, phase1key);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static int iwl_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct ieee80211_key_conf *key)
{
	struct iwl_priv *priv = hw->priv;
	const u8 *addr;
	int ret;
	u8 sta_id;
	bool is_default_wep_key = false;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (priv->cfg->mod_params->sw_crypto) {
		IWL_DEBUG_MAC80211(priv, "leave - hwcrypto disabled\n");
		return -EOPNOTSUPP;
	}
	addr = sta ? sta->addr : iwl_bcast_addr;
	sta_id = iwl_find_station(priv, addr);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_DEBUG_MAC80211(priv, "leave - %pM not in station map.\n",
				   addr);
		return -EINVAL;

	}

	mutex_lock(&priv->mutex);
	iwl_scan_cancel_timeout(priv, 100);
	mutex_unlock(&priv->mutex);

	/* If we are getting WEP group key and we didn't receive any key mapping
	 * so far, we are in legacy wep mode (group key only), otherwise we are
	 * in 1X mode.
	 * In legacy wep mode, we use another host command to the uCode */
	if (key->alg == ALG_WEP && sta_id == priv->hw_params.bcast_sta_id &&
		priv->iw_mode != NL80211_IFTYPE_AP) {
		if (cmd == SET_KEY)
			is_default_wep_key = !priv->key_mapping_key;
		else
			is_default_wep_key =
					(key->hw_key_idx == HW_KEY_DEFAULT);
	}

	switch (cmd) {
	case SET_KEY:
		if (is_default_wep_key)
			ret = iwl_set_default_wep_key(priv, key);
		else
			ret = iwl_set_dynamic_key(priv, key, sta_id);

		IWL_DEBUG_MAC80211(priv, "enable hwcrypto key\n");
		break;
	case DISABLE_KEY:
		if (is_default_wep_key)
			ret = iwl_remove_default_wep_key(priv, key);
		else
			ret = iwl_remove_dynamic_key(priv, key, sta_id);

		IWL_DEBUG_MAC80211(priv, "disable hwcrypto key\n");
		break;
	default:
		ret = -EINVAL;
	}

	IWL_DEBUG_MAC80211(priv, "leave\n");

	return ret;
}

static int iwl_mac_ampdu_action(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
			     enum ieee80211_ampdu_mlme_action action,
			     struct ieee80211_sta *sta, u16 tid, u16 *ssn)
{
	struct iwl_priv *priv = hw->priv;
	int ret;

	IWL_DEBUG_HT(priv, "A-MPDU action on addr %pM tid %d\n",
		     sta->addr, tid);

	if (!(priv->cfg->sku & IWL_SKU_N))
		return -EACCES;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		IWL_DEBUG_HT(priv, "start Rx\n");
		return iwl_sta_rx_agg_start(priv, sta->addr, tid, *ssn);
	case IEEE80211_AMPDU_RX_STOP:
		IWL_DEBUG_HT(priv, "stop Rx\n");
		ret = iwl_sta_rx_agg_stop(priv, sta->addr, tid);
		if (test_bit(STATUS_EXIT_PENDING, &priv->status))
			return 0;
		else
			return ret;
	case IEEE80211_AMPDU_TX_START:
		IWL_DEBUG_HT(priv, "start Tx\n");
		return iwl_tx_agg_start(priv, sta->addr, tid, ssn);
	case IEEE80211_AMPDU_TX_STOP:
		IWL_DEBUG_HT(priv, "stop Tx\n");
		ret = iwl_tx_agg_stop(priv, sta->addr, tid);
		if (test_bit(STATUS_EXIT_PENDING, &priv->status))
			return 0;
		else
			return ret;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		/* do nothing */
		return -EOPNOTSUPP;
	default:
		IWL_DEBUG_HT(priv, "unknown\n");
		return -EINVAL;
		break;
	}
	return 0;
}

static int iwl_mac_get_stats(struct ieee80211_hw *hw,
			     struct ieee80211_low_level_stats *stats)
{
	struct iwl_priv *priv = hw->priv;

	priv = hw->priv;
	IWL_DEBUG_MAC80211(priv, "enter\n");
	IWL_DEBUG_MAC80211(priv, "leave\n");

	return 0;
}

static void iwl_mac_sta_notify(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       enum sta_notify_cmd cmd,
			       struct ieee80211_sta *sta)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_station_priv *sta_priv = (void *)sta->drv_priv;
	int sta_id;

	/*
	 * TODO: We really should use this callback to
	 *	 actually maintain the station table in
	 *	 the device.
	 */

	switch (cmd) {
	case STA_NOTIFY_ADD:
		atomic_set(&sta_priv->pending_frames, 0);
		if (vif->type == NL80211_IFTYPE_AP)
			sta_priv->client = true;
		break;
	case STA_NOTIFY_SLEEP:
		WARN_ON(!sta_priv->client);
		sta_priv->asleep = true;
		if (atomic_read(&sta_priv->pending_frames) > 0)
			ieee80211_sta_block_awake(hw, sta, true);
		break;
	case STA_NOTIFY_AWAKE:
		WARN_ON(!sta_priv->client);
		if (!sta_priv->asleep)
			break;
		sta_priv->asleep = false;
		sta_id = iwl_find_station(priv, sta->addr);
		if (sta_id != IWL_INVALID_STATION)
			iwl_sta_modify_ps_wake(priv, sta_id);
		break;
	default:
		break;
	}
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

static ssize_t show_flags(struct device *d,
			  struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	return sprintf(buf, "0x%04X\n", priv->active_rxon.flags);
}

static ssize_t store_flags(struct device *d,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	unsigned long val;
	u32 flags;
	int ret = strict_strtoul(buf, 0, &val);
	if (ret)
		return ret;
	flags = (u32)val;

	mutex_lock(&priv->mutex);
	if (le32_to_cpu(priv->staging_rxon.flags) != flags) {
		/* Cancel any currently running scans... */
		if (iwl_scan_cancel_timeout(priv, 100))
			IWL_WARN(priv, "Could not cancel scan.\n");
		else {
			IWL_DEBUG_INFO(priv, "Commit rxon.flags = 0x%04X\n", flags);
			priv->staging_rxon.flags = cpu_to_le32(flags);
			iwlcore_commit_rxon(priv);
		}
	}
	mutex_unlock(&priv->mutex);

	return count;
}

static DEVICE_ATTR(flags, S_IWUSR | S_IRUGO, show_flags, store_flags);

static ssize_t show_filter_flags(struct device *d,
				 struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	return sprintf(buf, "0x%04X\n",
		le32_to_cpu(priv->active_rxon.filter_flags));
}

static ssize_t store_filter_flags(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	unsigned long val;
	u32 filter_flags;
	int ret = strict_strtoul(buf, 0, &val);
	if (ret)
		return ret;
	filter_flags = (u32)val;

	mutex_lock(&priv->mutex);
	if (le32_to_cpu(priv->staging_rxon.filter_flags) != filter_flags) {
		/* Cancel any currently running scans... */
		if (iwl_scan_cancel_timeout(priv, 100))
			IWL_WARN(priv, "Could not cancel scan.\n");
		else {
			IWL_DEBUG_INFO(priv, "Committing rxon.filter_flags = "
				       "0x%04X\n", filter_flags);
			priv->staging_rxon.filter_flags =
				cpu_to_le32(filter_flags);
			iwlcore_commit_rxon(priv);
		}
	}
	mutex_unlock(&priv->mutex);

	return count;
}

static DEVICE_ATTR(filter_flags, S_IWUSR | S_IRUGO, show_filter_flags,
		   store_filter_flags);


static ssize_t show_statistics(struct device *d,
			       struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	u32 size = sizeof(struct iwl_notif_statistics);
	u32 len = 0, ofs = 0;
	u8 *data = (u8 *)&priv->statistics;
	int rc = 0;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	mutex_lock(&priv->mutex);
	rc = iwl_send_statistics_request(priv, CMD_SYNC, false);
	mutex_unlock(&priv->mutex);

	if (rc) {
		len = sprintf(buf,
			      "Error sending statistics request: 0x%08X\n", rc);
		return len;
	}

	while (size && (PAGE_SIZE - len)) {
		hex_dump_to_buffer(data + ofs, size, 16, 1, buf + len,
				   PAGE_SIZE - len, 1);
		len = strlen(buf);
		if (PAGE_SIZE - len)
			buf[len++] = '\n';

		ofs += 16;
		size -= min(size, 16U);
	}

	return len;
}

static DEVICE_ATTR(statistics, S_IRUGO, show_statistics, NULL);

static ssize_t show_rts_ht_protection(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	return sprintf(buf, "%s\n",
		priv->cfg->use_rts_for_ht ? "RTS/CTS" : "CTS-to-self");
}

static ssize_t store_rts_ht_protection(struct device *d,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	unsigned long val;
	int ret;

	ret = strict_strtoul(buf, 10, &val);
	if (ret)
		IWL_INFO(priv, "Input is not in decimal form.\n");
	else {
		if (!iwl_is_associated(priv))
			priv->cfg->use_rts_for_ht = val ? true : false;
		else
			IWL_ERR(priv, "Sta associated with AP - "
				"Change protection mechanism is not allowed\n");
		ret = count;
	}
	return ret;
}

static DEVICE_ATTR(rts_ht_protection, S_IWUSR | S_IRUGO,
			show_rts_ht_protection, store_rts_ht_protection);


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
	INIT_DELAYED_WORK(&priv->init_alive_start, iwl_bg_init_alive_start);
	INIT_DELAYED_WORK(&priv->alive_start, iwl_bg_alive_start);

	iwl_setup_scan_deferred_work(priv);

	if (priv->cfg->ops->lib->setup_deferred_work)
		priv->cfg->ops->lib->setup_deferred_work(priv);

	init_timer(&priv->statistics_periodic);
	priv->statistics_periodic.data = (unsigned long)priv;
	priv->statistics_periodic.function = iwl_bg_statistics_periodic;

	init_timer(&priv->ucode_trace);
	priv->ucode_trace.data = (unsigned long)priv;
	priv->ucode_trace.function = iwl_bg_ucode_trace;

	if (!priv->cfg->use_isr_legacy)
		tasklet_init(&priv->irq_tasklet, (void (*)(unsigned long))
			iwl_irq_tasklet, (unsigned long)priv);
	else
		tasklet_init(&priv->irq_tasklet, (void (*)(unsigned long))
			iwl_irq_tasklet_legacy, (unsigned long)priv);
}

static void iwl_cancel_deferred_work(struct iwl_priv *priv)
{
	if (priv->cfg->ops->lib->cancel_deferred_work)
		priv->cfg->ops->lib->cancel_deferred_work(priv);

	cancel_delayed_work_sync(&priv->init_alive_start);
	cancel_delayed_work(&priv->scan_check);
	cancel_work_sync(&priv->start_internal_scan);
	cancel_delayed_work(&priv->alive_start);
	cancel_work_sync(&priv->beacon_update);
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

	priv->ibss_beacon = NULL;

	spin_lock_init(&priv->sta_lock);
	spin_lock_init(&priv->hcmd_lock);

	INIT_LIST_HEAD(&priv->free_frames);

	mutex_init(&priv->mutex);
	mutex_init(&priv->sync_cmd_mutex);

	/* Clear the driver's (not device's) station table */
	iwl_clear_stations_table(priv);

	priv->ieee_channels = NULL;
	priv->ieee_rates = NULL;
	priv->band = IEEE80211_BAND_2GHZ;

	priv->iw_mode = NL80211_IFTYPE_STATION;
	priv->current_ht_config.smps = IEEE80211_SMPS_STATIC;
	priv->missed_beacon_threshold = IWL_MISSED_BEACON_THRESHOLD_DEF;

	/* initialize force reset */
	priv->force_reset[IWL_RF_RESET].reset_duration =
		IWL_DELAY_NEXT_FORCE_RF_RESET;
	priv->force_reset[IWL_FW_RESET].reset_duration =
		IWL_DELAY_NEXT_FORCE_FW_RELOAD;

	/* Choose which receivers/antennas to use */
	if (priv->cfg->ops->hcmd->set_rxon_chain)
		priv->cfg->ops->hcmd->set_rxon_chain(priv);

	iwl_init_scan_params(priv);

	iwl_reset_qos(priv);

	priv->qos_data.qos_active = 0;
	priv->qos_data.qos_cap.val = 0;

	priv->rates_mask = IWL_RATES_MASK;
	/* Set the tx_power_user_lmt to the lowest power level
	 * this value will get overwritten by channel max power avg
	 * from eeprom */
	priv->tx_power_user_lmt = IWL_TX_POWER_TARGET_POWER_MIN;

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
	kfree(priv->scan);
}

static struct attribute *iwl_sysfs_entries[] = {
	&dev_attr_flags.attr,
	&dev_attr_filter_flags.attr,
	&dev_attr_statistics.attr,
	&dev_attr_temperature.attr,
	&dev_attr_tx_power.attr,
	&dev_attr_rts_ht_protection.attr,
#ifdef CONFIG_IWLWIFI_DEBUG
	&dev_attr_debug_level.attr,
#endif
	NULL
};

static struct attribute_group iwl_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = iwl_sysfs_entries,
};

static struct ieee80211_ops iwl_hw_ops = {
	.tx = iwl_mac_tx,
	.start = iwl_mac_start,
	.stop = iwl_mac_stop,
	.add_interface = iwl_mac_add_interface,
	.remove_interface = iwl_mac_remove_interface,
	.config = iwl_mac_config,
	.configure_filter = iwl_configure_filter,
	.set_key = iwl_mac_set_key,
	.update_tkip_key = iwl_mac_update_tkip_key,
	.get_stats = iwl_mac_get_stats,
	.conf_tx = iwl_mac_conf_tx,
	.reset_tsf = iwl_mac_reset_tsf,
	.bss_info_changed = iwl_bss_info_changed,
	.ampdu_action = iwl_mac_ampdu_action,
	.hw_scan = iwl_mac_hw_scan,
	.sta_notify = iwl_mac_sta_notify,
};

static int iwl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = 0;
	struct iwl_priv *priv;
	struct ieee80211_hw *hw;
	struct iwl_cfg *cfg = (struct iwl_cfg *)(ent->driver_data);
	unsigned long flags;
	u16 pci_cmd;

	/************************
	 * 1. Allocating HW data
	 ************************/

	/* Disabling hardware scan means that mac80211 will perform scans
	 * "the hard way", rather than using device's scan. */
	if (cfg->mod_params->disable_hw_scan) {
		if (iwl_debug_level & IWL_DL_INFO)
			dev_printk(KERN_DEBUG, &(pdev->dev),
				   "Disabling hw_scan\n");
		iwl_hw_ops.hw_scan = NULL;
	}

	hw = iwl_alloc_all(cfg, &iwl_hw_ops);
	if (!hw) {
		err = -ENOMEM;
		goto out;
	}
	priv = hw->priv;
	/* At this point both hw and priv are allocated. */

	SET_IEEE80211_DEV(hw, &pdev->dev);

	IWL_DEBUG_INFO(priv, "*** LOAD DRIVER ***\n");
	priv->cfg = cfg;
	priv->pci_dev = pdev;
	priv->inta_mask = CSR_INI_SET_MASK;

#ifdef CONFIG_IWLWIFI_DEBUG
	atomic_set(&priv->restrict_refcnt, 0);
#endif
	if (iwl_alloc_traffic_mem(priv))
		IWL_ERR(priv, "Not enough memory to generate traffic log\n");

	/**************************
	 * 2. Initializing PCI bus
	 **************************/
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

	iwl_hw_detect(priv);
	IWL_INFO(priv, "Detected Intel Wireless WiFi Link %s REV=0x%X\n",
		priv->cfg->name, priv->hw_rev);

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	iwl_prepare_card_hw(priv);
	if (!priv->hw_ready) {
		IWL_WARN(priv, "Failed, HW not ready\n");
		goto out_iounmap;
	}

	/*****************
	 * 4. Read EEPROM
	 *****************/
	/* Read the EEPROM */
	err = iwl_eeprom_init(priv);
	if (err) {
		IWL_ERR(priv, "Unable to init EEPROM\n");
		goto out_iounmap;
	}
	err = iwl_eeprom_check_version(priv);
	if (err)
		goto out_free_eeprom;

	/* extract MAC Address */
	iwl_eeprom_get_mac(priv, priv->mac_addr);
	IWL_DEBUG_INFO(priv, "MAC address: %pM\n", priv->mac_addr);
	SET_IEEE80211_PERM_ADDR(priv->hw, priv->mac_addr);

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
	err = request_irq(priv->pci_dev->irq, priv->cfg->ops->lib->isr,
			  IRQF_SHARED, DRV_NAME, priv);
	if (err) {
		IWL_ERR(priv, "Error allocating IRQ %d\n", priv->pci_dev->irq);
		goto out_disable_msi;
	}
	err = sysfs_create_group(&pdev->dev.kobj, &iwl_attribute_group);
	if (err) {
		IWL_ERR(priv, "failed to create sysfs device attributes\n");
		goto out_free_irq;
	}

	iwl_setup_deferred_work(priv);
	iwl_setup_rx_handlers(priv);

	/*********************************************
	 * 8. Enable interrupts and read RFKILL state
	 *********************************************/

	/* enable interrupts if needed: hw bug w/a */
	pci_read_config_word(priv->pci_dev, PCI_COMMAND, &pci_cmd);
	if (pci_cmd & PCI_COMMAND_INTX_DISABLE) {
		pci_cmd &= ~PCI_COMMAND_INTX_DISABLE;
		pci_write_config_word(priv->pci_dev, PCI_COMMAND, pci_cmd);
	}

	iwl_enable_interrupts(priv);

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (iwl_read32(priv, CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		set_bit(STATUS_RF_KILL_HW, &priv->status);

	wiphy_rfkill_set_hw_state(priv->hw->wiphy,
		test_bit(STATUS_RF_KILL_HW, &priv->status));

	iwl_power_initialize(priv);
	iwl_tt_initialize(priv);

	err = iwl_request_firmware(priv, true);
	if (err)
		goto out_remove_sysfs;

	return 0;

 out_remove_sysfs:
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;
	sysfs_remove_group(&pdev->dev.kobj, &iwl_attribute_group);
 out_free_irq:
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

	IWL_DEBUG_INFO(priv, "*** UNLOAD DRIVER ***\n");

	iwl_dbgfs_unregister(priv);
	sysfs_remove_group(&pdev->dev.kobj, &iwl_attribute_group);

	/* ieee80211_unregister_hw call wil cause iwl_mac_stop to
	 * to be called and iwl_down since we are removing the device
	 * we need to set STATUS_EXIT_PENDING bit.
	 */
	set_bit(STATUS_EXIT_PENDING, &priv->status);
	if (priv->mac80211_registered) {
		ieee80211_unregister_hw(priv->hw);
		priv->mac80211_registered = 0;
	} else {
		iwl_down(priv);
	}

	/*
	 * Make sure device is reset to low power before unloading driver.
	 * This may be redundant with iwl_down(), but there are paths to
	 * run iwl_down() without calling apm_ops.stop(), and there are
	 * paths to avoid running iwl_down() at all before leaving driver.
	 * This (inexpensive) call *makes sure* device is reset.
	 */
	priv->cfg->ops->lib->apm_ops.stop(priv);

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
		iwl_rx_queue_free(priv, &priv->rxq);
	iwl_hw_txq_ctx_free(priv);

	iwl_clear_stations_table(priv);
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

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	ieee80211_free_hw(priv->hw);
}


/*****************************************************************************
 *
 * driver and module entry point
 *
 *****************************************************************************/

/* Hardware specific file defines the PCI IDs table for that hardware module */
static DEFINE_PCI_DEVICE_TABLE(iwl_hw_card_ids) = {
#ifdef CONFIG_IWL4965
	{IWL_PCI_DEVICE(0x4229, PCI_ANY_ID, iwl4965_agn_cfg)},
	{IWL_PCI_DEVICE(0x4230, PCI_ANY_ID, iwl4965_agn_cfg)},
#endif /* CONFIG_IWL4965 */
#ifdef CONFIG_IWL5000
/* 5100 Series WiFi */
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

/* 6x50 WiFi/WiMax Series */
	{IWL_PCI_DEVICE(0x0087, 0x1301, iwl6050_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1306, iwl6050_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1321, iwl6050_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1326, iwl6050_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0089, 0x1311, iwl6050_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0089, 0x1316, iwl6050_2abg_cfg)},

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
#endif /* CONFIG_IWL5000 */

	{0}
};
MODULE_DEVICE_TABLE(pci, iwl_hw_card_ids);

static struct pci_driver iwl_driver = {
	.name = DRV_NAME,
	.id_table = iwl_hw_card_ids,
	.probe = iwl_pci_probe,
	.remove = __devexit_p(iwl_pci_remove),
#ifdef CONFIG_PM
	.suspend = iwl_pci_suspend,
	.resume = iwl_pci_resume,
#endif
};

static int __init iwl_init(void)
{

	int ret;
	printk(KERN_INFO DRV_NAME ": " DRV_DESCRIPTION ", " DRV_VERSION "\n");
	printk(KERN_INFO DRV_NAME ": " DRV_COPYRIGHT "\n");

	ret = iwlagn_rate_control_register();
	if (ret) {
		printk(KERN_ERR DRV_NAME
		       "Unable to register rate control algorithm: %d\n", ret);
		return ret;
	}

	ret = pci_register_driver(&iwl_driver);
	if (ret) {
		printk(KERN_ERR DRV_NAME "Unable to initialize PCI module\n");
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
module_param_named(debug50, iwl_debug_level, uint, S_IRUGO);
MODULE_PARM_DESC(debug50, "50XX debug output mask (deprecated)");
module_param_named(debug, iwl_debug_level, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "debug output mask");
#endif

