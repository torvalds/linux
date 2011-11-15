/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-sta.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-4965-hw.h"
#include "iwl-4965.h"

/*
 * mac80211 queues, ACs, hardware queues, FIFOs.
 *
 * Cf. http://wireless.kernel.org/en/developers/Documentation/mac80211/queues
 *
 * Mac80211 uses the following numbers, which we get as from it
 * by way of skb_get_queue_mapping(skb):
 *
 *	VO	0
 *	VI	1
 *	BE	2
 *	BK	3
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
 * iwl-4965.c), the AC->hw queue mapping is the identity
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

static inline int il4965_get_ac_from_tid(u16 tid)
{
	if (likely(tid < ARRAY_SIZE(tid_to_ac)))
		return tid_to_ac[tid];

	/* no support for TIDs 8-15 yet */
	return -EINVAL;
}

static inline int
il4965_get_fifo_from_tid(struct il_rxon_context *ctx, u16 tid)
{
	if (likely(tid < ARRAY_SIZE(tid_to_ac)))
		return ctx->ac_to_fifo[tid_to_ac[tid]];

	/* no support for TIDs 8-15 yet */
	return -EINVAL;
}

/*
 * handle build REPLY_TX command notification.
 */
static void il4965_tx_cmd_build_basic(struct il_priv *il,
					struct sk_buff *skb,
					struct il_tx_cmd *tx_cmd,
					struct ieee80211_tx_info *info,
					struct ieee80211_hdr *hdr,
					u8 std_id)
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

#define RTS_DFAULT_RETRY_LIMIT		60

static void il4965_tx_cmd_build_rate(struct il_priv *il,
			      struct il_tx_cmd *tx_cmd,
			      struct ieee80211_tx_info *info,
			      __le16 fc)
{
	u32 rate_flags;
	int rate_idx;
	u8 rts_retry_limit;
	u8 data_retry_limit;
	u8 rate_plcp;

	/* Set retry limit on DATA packets and Probe Responses*/
	if (ieee80211_is_probe_resp(fc))
		data_retry_limit = 3;
	else
		data_retry_limit = IL4965_DEFAULT_TX_RETRY;
	tx_cmd->data_retry_limit = data_retry_limit;

	/* Set retry limit on RTS packets */
	rts_retry_limit = RTS_DFAULT_RETRY_LIMIT;
	if (data_retry_limit < rts_retry_limit)
		rts_retry_limit = data_retry_limit;
	tx_cmd->rts_retry_limit = rts_retry_limit;

	/* DATA packets will use the uCode station table for rate/antenna
	 * selection */
	if (ieee80211_is_data(fc)) {
		tx_cmd->initial_rate_index = 0;
		tx_cmd->tx_flags |= TX_CMD_FLG_STA_RATE_MSK;
		return;
	}

	/**
	 * If the current TX rate stored in mac80211 has the MCS bit set, it's
	 * not really a TX rate.  Thus, we use the lowest supported rate for
	 * this band.  Also use the lowest supported rate if the stored rate
	 * index is invalid.
	 */
	rate_idx = info->control.rates[0].idx;
	if ((info->control.rates[0].flags & IEEE80211_TX_RC_MCS) ||
	    rate_idx < 0 || rate_idx > IL_RATE_COUNT_LEGACY)
		rate_idx = rate_lowest_index(&il->bands[info->band],
				info->control.sta);
	/* For 5 GHZ band, remap mac80211 rate indices into driver indices */
	if (info->band == IEEE80211_BAND_5GHZ)
		rate_idx += IL_FIRST_OFDM_RATE;
	/* Get PLCP rate for tx_cmd->rate_n_flags */
	rate_plcp = il_rates[rate_idx].plcp;
	/* Zero out flags for this packet */
	rate_flags = 0;

	/* Set CCK flag as needed */
	if (rate_idx >= IL_FIRST_CCK_RATE && rate_idx <= IL_LAST_CCK_RATE)
		rate_flags |= RATE_MCS_CCK_MSK;

	/* Set up antennas */
	il->mgmt_tx_ant = il4965_toggle_tx_ant(il, il->mgmt_tx_ant,
				      il->hw_params.valid_tx_ant);

	rate_flags |= il4965_ant_idx_to_flags(il->mgmt_tx_ant);

	/* Set the rate in the TX cmd */
	tx_cmd->rate_n_flags = il4965_hw_set_rate_n_flags(rate_plcp, rate_flags);
}

static void il4965_tx_cmd_build_hwcrypto(struct il_priv *il,
				      struct ieee80211_tx_info *info,
				      struct il_tx_cmd *tx_cmd,
				      struct sk_buff *skb_frag,
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
		/* fall through */
	case WLAN_CIPHER_SUITE_WEP40:
		tx_cmd->sec_ctl |= (TX_CMD_SEC_WEP |
			(keyconf->keyidx & TX_CMD_SEC_MSK) << TX_CMD_SEC_SHIFT);

		memcpy(&tx_cmd->key[3], keyconf->key, keyconf->keylen);

		D_TX("Configuring packet for WEP encryption "
			     "with key %d\n", keyconf->keyidx);
		break;

	default:
		IL_ERR("Unknown encode cipher %x\n", keyconf->cipher);
		break;
	}
}

/*
 * start REPLY_TX command process
 */
int il4965_tx_skb(struct il_priv *il, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_sta *sta = info->control.sta;
	struct il_station_priv *sta_priv = NULL;
	struct il_tx_queue *txq;
	struct il_queue *q;
	struct il_device_cmd *out_cmd;
	struct il_cmd_meta *out_meta;
	struct il_tx_cmd *tx_cmd;
	struct il_rxon_context *ctx = &il->ctx;
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

	if (info->control.vif)
		ctx = il_rxon_ctx_from_vif(info->control.vif);

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
		sta_id = ctx->bcast_sta_id;
	else {
		/* Find index into station table for destination station */
		sta_id = il_sta_id_or_broadcast(il, ctx, info->control.sta);

		if (sta_id == IL_INVALID_STATION) {
			D_DROP("Dropping - INVALID STATION: %pM\n",
				       hdr->addr1);
			goto drop_unlock;
		}
	}

	D_TX("station Id %d\n", sta_id);

	if (sta)
		sta_priv = (void *)sta->drv_priv;

	if (sta_priv && sta_priv->asleep &&
	    (info->flags & IEEE80211_TX_CTL_POLL_RESPONSE)) {
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

	/*
	 * Send this frame after DTIM -- there's a special queue
	 * reserved for this for contexts that support AP mode.
	 */
	if (info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM) {
		txq_id = ctx->mcast_queue;
		/*
		 * The microcode will clear the more data
		 * bit in the last frame it transmits.
		 */
		hdr->frame_control |=
			cpu_to_le16(IEEE80211_FCTL_MOREDATA);
	} else
		txq_id = ctx->ac_to_queue[skb_get_queue_mapping(skb)];

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
		hdr->seq_ctrl = hdr->seq_ctrl &
				cpu_to_le16(IEEE80211_SCTL_FRAG);
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

	/* Set up driver data for this TFD */
	memset(&(txq->txb[q->write_ptr]), 0, sizeof(struct il_tx_info));
	txq->txb[q->write_ptr].skb = skb;
	txq->txb[q->write_ptr].ctx = ctx;

	/* Set up first empty entry in queue's array of Tx/cmd buffers */
	out_cmd = txq->cmd[q->write_ptr];
	out_meta = &txq->meta[q->write_ptr];
	tx_cmd = &out_cmd->cmd.tx;
	memset(&out_cmd->hdr, 0, sizeof(out_cmd->hdr));
	memset(tx_cmd, 0, sizeof(struct il_tx_cmd));

	/*
	 * Set up the Tx-command (not MAC!) header.
	 * Store the chosen Tx queue and TFD index within the sequence field;
	 * after Tx, uCode's Tx response will return this value so driver can
	 * locate the frame within the tx queue and do post-tx processing.
	 */
	out_cmd->hdr.cmd = REPLY_TX;
	out_cmd->hdr.sequence = cpu_to_le16((u16)(QUEUE_TO_SEQ(txq_id) |
				INDEX_TO_SEQ(q->write_ptr)));

	/* Copy MAC header from skb into command buffer */
	memcpy(tx_cmd->hdr, hdr, hdr_len);


	/* Total # bytes to be transmitted */
	len = (u16)skb->len;
	tx_cmd->len = cpu_to_le16(len);

	if (info->control.hw_key)
		il4965_tx_cmd_build_hwcrypto(il, info, tx_cmd, skb, sta_id);

	/* TODO need this for burst mode later on */
	il4965_tx_cmd_build_basic(il, skb, tx_cmd, info, hdr, sta_id);
	il_dbg_log_tx_data_frame(il, len, hdr);

	il4965_tx_cmd_build_rate(il, tx_cmd, info, fc);

	il_update_stats(il, true, fc, len);
	/*
	 * Use the first empty entry in this queue's command buffer array
	 * to contain the Tx command and MAC header concatenated together
	 * (payload data will be in another buffer).
	 * Size of this varies, due to varying MAC header length.
	 * If end is not dword aligned, we'll have 2 extra bytes at the end
	 * of the MAC header (device reads on dword boundaries).
	 * We'll tell device about this padding later.
	 */
	len = sizeof(struct il_tx_cmd) +
		sizeof(struct il_cmd_header) + hdr_len;
	firstlen = (len + 3) & ~3;

	/* Tell NIC about any 2-byte padding after MAC header */
	if (firstlen != len)
		tx_cmd->tx_flags |= TX_CMD_FLG_MH_PAD_MSK;

	/* Physical address of this Tx command's header (not MAC header!),
	 * within command buffer array. */
	txcmd_phys = pci_map_single(il->pci_dev,
				    &out_cmd->hdr, firstlen,
				    PCI_DMA_BIDIRECTIONAL);
	dma_unmap_addr_set(out_meta, mapping, txcmd_phys);
	dma_unmap_len_set(out_meta, len, firstlen);
	/* Add buffer containing Tx command and MAC(!) header to TFD's
	 * first entry */
	il->cfg->ops->lib->txq_attach_buf_to_tfd(il, txq,
						   txcmd_phys, firstlen, 1, 0);

	if (!ieee80211_has_morefrags(hdr->frame_control)) {
		txq->need_update = 1;
	} else {
		wait_write_ptr = 1;
		txq->need_update = 0;
	}

	/* Set up TFD's 2nd entry to point directly to remainder of skb,
	 * if any (802.11 null frames have no payload). */
	secondlen = skb->len - hdr_len;
	if (secondlen > 0) {
		phys_addr = pci_map_single(il->pci_dev, skb->data + hdr_len,
					   secondlen, PCI_DMA_TODEVICE);
		il->cfg->ops->lib->txq_attach_buf_to_tfd(il, txq,
							   phys_addr, secondlen,
							   0, 0);
	}

	scratch_phys = txcmd_phys + sizeof(struct il_cmd_header) +
				offsetof(struct il_tx_cmd, scratch);

	/* take back ownership of DMA buffer to enable update */
	pci_dma_sync_single_for_cpu(il->pci_dev, txcmd_phys,
				    firstlen, PCI_DMA_BIDIRECTIONAL);
	tx_cmd->dram_lsb_ptr = cpu_to_le32(scratch_phys);
	tx_cmd->dram_msb_ptr = il_get_dma_hi_addr(scratch_phys);

	D_TX("sequence nr = 0X%x\n",
		     le16_to_cpu(out_cmd->hdr.sequence));
	D_TX("tx_flags = 0X%x\n", le32_to_cpu(tx_cmd->tx_flags));
	il_print_hex_dump(il, IL_DL_TX, (u8 *)tx_cmd, sizeof(*tx_cmd));
	il_print_hex_dump(il, IL_DL_TX, (u8 *)tx_cmd->hdr, hdr_len);

	/* Set up entry for this TFD in Tx byte-count array */
	if (info->flags & IEEE80211_TX_CTL_AMPDU)
		il->cfg->ops->lib->txq_update_byte_cnt_tbl(il, txq,
						     le16_to_cpu(tx_cmd->len));

	pci_dma_sync_single_for_device(il->pci_dev, txcmd_phys,
				       firstlen, PCI_DMA_BIDIRECTIONAL);

	/* Tell device the write index *just past* this latest filled TFD */
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

static inline int il4965_alloc_dma_ptr(struct il_priv *il,
				    struct il_dma_ptr *ptr, size_t size)
{
	ptr->addr = dma_alloc_coherent(&il->pci_dev->dev, size, &ptr->dma,
				       GFP_KERNEL);
	if (!ptr->addr)
		return -ENOMEM;
	ptr->size = size;
	return 0;
}

static inline void il4965_free_dma_ptr(struct il_priv *il,
				    struct il_dma_ptr *ptr)
{
	if (unlikely(!ptr->addr))
		return;

	dma_free_coherent(&il->pci_dev->dev, ptr->size, ptr->addr, ptr->dma);
	memset(ptr, 0, sizeof(*ptr));
}

/**
 * il4965_hw_txq_ctx_free - Free TXQ Context
 *
 * Destroy all TX DMA queues and structures
 */
void il4965_hw_txq_ctx_free(struct il_priv *il)
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
	il_txq_mem(il);
}

/**
 * il4965_txq_ctx_alloc - allocate TX queue context
 * Allocate all Tx DMA structures and initialize them
 *
 * @param il
 * @return error code
 */
int il4965_txq_ctx_alloc(struct il_priv *il)
{
	int ret;
	int txq_id, slots_num;
	unsigned long flags;

	/* Free all tx/cmd queues and keep-warm buffer */
	il4965_hw_txq_ctx_free(il);

	ret = il4965_alloc_dma_ptr(il, &il->scd_bc_tbls,
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
	il_wr(il, FH_KW_MEM_ADDR_REG, il->kw.dma >> 4);

	spin_unlock_irqrestore(&il->lock, flags);

	/* Alloc and init all Tx queues, including the command queue (#4/#9) */
	for (txq_id = 0; txq_id < il->hw_params.max_txq_num; txq_id++) {
		slots_num = (txq_id == il->cmd_queue) ?
					TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
		ret = il_tx_queue_init(il,
					&il->txq[txq_id], slots_num,
				       txq_id);
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

void il4965_txq_ctx_reset(struct il_priv *il)
{
	int txq_id, slots_num;
	unsigned long flags;

	spin_lock_irqsave(&il->lock, flags);

	/* Turn off all Tx DMA fifos */
	il4965_txq_set_sched(il, 0);

	/* Tell NIC where to find the "keep warm" buffer */
	il_wr(il, FH_KW_MEM_ADDR_REG, il->kw.dma >> 4);

	spin_unlock_irqrestore(&il->lock, flags);

	/* Alloc and init all Tx queues, including the command queue (#4) */
	for (txq_id = 0; txq_id < il->hw_params.max_txq_num; txq_id++) {
		slots_num = txq_id == il->cmd_queue ?
			    TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
		il_tx_queue_reset(il, &il->txq[txq_id],
						slots_num, txq_id);
	}
}

/**
 * il4965_txq_ctx_stop - Stop all Tx DMA channels
 */
void il4965_txq_ctx_stop(struct il_priv *il)
{
	int ch, txq_id;
	unsigned long flags;

	/* Turn off all Tx DMA fifos */
	spin_lock_irqsave(&il->lock, flags);

	il4965_txq_set_sched(il, 0);

	/* Stop each Tx DMA channel, and wait for it to be idle */
	for (ch = 0; ch < il->hw_params.dma_chnl_num; ch++) {
		il_wr(il,
				FH_TCSR_CHNL_TX_CONFIG_REG(ch), 0x0);
		if (il_poll_bit(il, FH_TSSR_TX_STATUS_REG,
				    FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(ch),
				    1000))
			IL_ERR("Failing on timeout while stopping"
			    " DMA channel %d [0x%08x]", ch,
			    il_rd(il,
					FH_TSSR_TX_STATUS_REG));
	}
	spin_unlock_irqrestore(&il->lock, flags);

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
 * Find first available (lowest unused) Tx Queue, mark it "active".
 * Called only when finding queue for aggregation.
 * Should never return anything < 7, because they should already
 * be in use as EDCA AC (0-3), Command (4), reserved (5, 6)
 */
static int il4965_txq_ctx_activate_free(struct il_priv *il)
{
	int txq_id;

	for (txq_id = 0; txq_id < il->hw_params.max_txq_num; txq_id++)
		if (!test_and_set_bit(txq_id, &il->txq_ctx_active_msk))
			return txq_id;
	return -1;
}

/**
 * il4965_tx_queue_stop_scheduler - Stop queue, but keep configuration
 */
static void il4965_tx_queue_stop_scheduler(struct il_priv *il,
					    u16 txq_id)
{
	/* Simply stop the queue, but don't change any configuration;
	 * the SCD_ACT_EN bit is the write-enable mask for the ACTIVE bit. */
	il_wr_prph(il,
		IL49_SCD_QUEUE_STATUS_BITS(txq_id),
		(0 << IL49_SCD_QUEUE_STTS_REG_POS_ACTIVE)|
		(1 << IL49_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN));
}

/**
 * il4965_tx_queue_set_q2ratid - Map unique receiver/tid combination to a queue
 */
static int il4965_tx_queue_set_q2ratid(struct il_priv *il, u16 ra_tid,
					u16 txq_id)
{
	u32 tbl_dw_addr;
	u32 tbl_dw;
	u16 scd_q2ratid;

	scd_q2ratid = ra_tid & IL_SCD_QUEUE_RA_TID_MAP_RATID_MSK;

	tbl_dw_addr = il->scd_base_addr +
			IL49_SCD_TRANSLATE_TBL_OFFSET_QUEUE(txq_id);

	tbl_dw = il_read_targ_mem(il, tbl_dw_addr);

	if (txq_id & 0x1)
		tbl_dw = (scd_q2ratid << 16) | (tbl_dw & 0x0000FFFF);
	else
		tbl_dw = scd_q2ratid | (tbl_dw & 0xFFFF0000);

	il_write_targ_mem(il, tbl_dw_addr, tbl_dw);

	return 0;
}

/**
 * il4965_tx_queue_agg_enable - Set up & enable aggregation for selected queue
 *
 * NOTE:  txq_id must be greater than IL49_FIRST_AMPDU_QUEUE,
 *        i.e. it must be one of the higher queues used for aggregation
 */
static int il4965_txq_agg_enable(struct il_priv *il, int txq_id,
				  int tx_fifo, int sta_id, int tid, u16 ssn_idx)
{
	unsigned long flags;
	u16 ra_tid;
	int ret;

	if ((IL49_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IL49_FIRST_AMPDU_QUEUE +
		il->cfg->base_params->num_of_ampdu_queues <= txq_id)) {
		IL_WARN(
			"queue number out of range: %d, must be %d to %d\n",
			txq_id, IL49_FIRST_AMPDU_QUEUE,
			IL49_FIRST_AMPDU_QUEUE +
			il->cfg->base_params->num_of_ampdu_queues - 1);
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

	/* Place first TFD at index corresponding to start sequence number.
	 * Assumes that ssn_idx is valid (!= 0xFFF) */
	il->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	il->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	il4965_set_wr_ptrs(il, txq_id, ssn_idx);

	/* Set up Tx window size and frame limit for this queue */
	il_write_targ_mem(il,
		il->scd_base_addr + IL49_SCD_CONTEXT_QUEUE_OFFSET(txq_id),
		(SCD_WIN_SIZE << IL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_POS) &
		IL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_MSK);

	il_write_targ_mem(il, il->scd_base_addr +
		IL49_SCD_CONTEXT_QUEUE_OFFSET(txq_id) + sizeof(u32),
		(SCD_FRAME_LIMIT << IL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS)
		& IL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK);

	il_set_bits_prph(il, IL49_SCD_INTERRUPT_MASK, (1 << txq_id));

	/* Set up Status area in SRAM, map to Tx DMA/FIFO, activate the queue */
	il4965_tx_queue_set_status(il, &il->txq[txq_id], tx_fifo, 1);

	spin_unlock_irqrestore(&il->lock, flags);

	return 0;
}


int il4965_tx_agg_start(struct il_priv *il, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn)
{
	int sta_id;
	int tx_fifo;
	int txq_id;
	int ret;
	unsigned long flags;
	struct il_tid_data *tid_data;

	tx_fifo = il4965_get_fifo_from_tid(il_rxon_ctx_from_vif(vif), tid);
	if (unlikely(tx_fifo < 0))
		return tx_fifo;

	IL_WARN("%s on ra = %pM tid = %d\n",
			__func__, sta->addr, tid);

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
	*ssn = SEQ_TO_SN(tid_data->seq_number);
	tid_data->agg.txq_id = txq_id;
	il_set_swq_id(&il->txq[txq_id],
				il4965_get_ac_from_tid(tid), txq_id);
	spin_unlock_irqrestore(&il->sta_lock, flags);

	ret = il4965_txq_agg_enable(il, txq_id, tx_fifo,
						  sta_id, tid, *ssn);
	if (ret)
		return ret;

	spin_lock_irqsave(&il->sta_lock, flags);
	tid_data = &il->stations[sta_id].tid[tid];
	if (tid_data->tfds_in_queue == 0) {
		D_HT("HW queue is empty\n");
		tid_data->agg.state = IL_AGG_ON;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
	} else {
		D_HT(
			"HW queue is NOT empty: %d packets in HW queue\n",
			     tid_data->tfds_in_queue);
		tid_data->agg.state = IL_EMPTYING_HW_QUEUE_ADDBA;
	}
	spin_unlock_irqrestore(&il->sta_lock, flags);
	return ret;
}

/**
 * txq_id must be greater than IL49_FIRST_AMPDU_QUEUE
 * il->lock must be held by the caller
 */
static int il4965_txq_agg_disable(struct il_priv *il, u16 txq_id,
				   u16 ssn_idx, u8 tx_fifo)
{
	if ((IL49_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IL49_FIRST_AMPDU_QUEUE +
		il->cfg->base_params->num_of_ampdu_queues <= txq_id)) {
		IL_WARN(
			"queue number out of range: %d, must be %d to %d\n",
			txq_id, IL49_FIRST_AMPDU_QUEUE,
			IL49_FIRST_AMPDU_QUEUE +
			il->cfg->base_params->num_of_ampdu_queues - 1);
		return -EINVAL;
	}

	il4965_tx_queue_stop_scheduler(il, txq_id);

	il_clear_bits_prph(il,
			IL49_SCD_QUEUECHAIN_SEL, (1 << txq_id));

	il->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	il->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	/* supposes that ssn_idx is valid (!= 0xFFF) */
	il4965_set_wr_ptrs(il, txq_id, ssn_idx);

	il_clear_bits_prph(il,
			 IL49_SCD_INTERRUPT_MASK, (1 << txq_id));
	il_txq_ctx_deactivate(il, txq_id);
	il4965_tx_queue_set_status(il, &il->txq[txq_id], tx_fifo, 0);

	return 0;
}

int il4965_tx_agg_stop(struct il_priv *il, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, u16 tid)
{
	int tx_fifo_id, txq_id, sta_id, ssn;
	struct il_tid_data *tid_data;
	int write_ptr, read_ptr;
	unsigned long flags;

	tx_fifo_id = il4965_get_fifo_from_tid(il_rxon_ctx_from_vif(vif), tid);
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

int il4965_txq_check_empty(struct il_priv *il,
			   int sta_id, u8 tid, int txq_id)
{
	struct il_queue *q = &il->txq[txq_id].q;
	u8 *addr = il->stations[sta_id].sta.sta.addr;
	struct il_tid_data *tid_data = &il->stations[sta_id].tid[tid];
	struct il_rxon_context *ctx;

	ctx = &il->ctx;

	lockdep_assert_held(&il->sta_lock);

	switch (il->stations[sta_id].tid[tid].agg.state) {
	case IL_EMPTYING_HW_QUEUE_DELBA:
		/* We are reclaiming the last packet of the */
		/* aggregated HW queue */
		if (txq_id  == tid_data->agg.txq_id &&
		    q->read_ptr == q->write_ptr) {
			u16 ssn = SEQ_TO_SN(tid_data->seq_number);
			int tx_fifo = il4965_get_fifo_from_tid(ctx, tid);
			D_HT(
				"HW queue empty: continue DELBA flow\n");
			il4965_txq_agg_disable(il, txq_id, ssn, tx_fifo);
			tid_data->agg.state = IL_AGG_OFF;
			ieee80211_stop_tx_ba_cb_irqsafe(ctx->vif, addr, tid);
		}
		break;
	case IL_EMPTYING_HW_QUEUE_ADDBA:
		/* We are reclaiming the last packet of the queue */
		if (tid_data->tfds_in_queue == 0) {
			D_HT(
				"HW queue empty: continue ADDBA flow\n");
			tid_data->agg.state = IL_AGG_ON;
			ieee80211_start_tx_ba_cb_irqsafe(ctx->vif, addr, tid);
		}
		break;
	}

	return 0;
}

static void il4965_non_agg_tx_status(struct il_priv *il,
				     struct il_rxon_context *ctx,
				     const u8 *addr1)
{
	struct ieee80211_sta *sta;
	struct il_station_priv *sta_priv;

	rcu_read_lock();
	sta = ieee80211_find_sta(ctx->vif, addr1);
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
il4965_tx_status(struct il_priv *il, struct il_tx_info *tx_info,
			     bool is_agg)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx_info->skb->data;

	if (!is_agg)
		il4965_non_agg_tx_status(il, tx_info->ctx, hdr->addr1);

	ieee80211_tx_status_irqsafe(il->hw, tx_info->skb);
}

int il4965_tx_queue_reclaim(struct il_priv *il, int txq_id, int index)
{
	struct il_tx_queue *txq = &il->txq[txq_id];
	struct il_queue *q = &txq->q;
	struct il_tx_info *tx_info;
	int nfreed = 0;
	struct ieee80211_hdr *hdr;

	if (index >= q->n_bd || il_queue_used(q, index) == 0) {
		IL_ERR("Read index for DMA queue txq id (%d), index %d, "
			  "is out of range [0-%d] %d %d.\n", txq_id,
			  index, q->n_bd, q->write_ptr, q->read_ptr);
		return 0;
	}

	for (index = il_queue_inc_wrap(index, q->n_bd);
	     q->read_ptr != index;
	     q->read_ptr = il_queue_inc_wrap(q->read_ptr, q->n_bd)) {

		tx_info = &txq->txb[txq->q.read_ptr];

		if (WARN_ON_ONCE(tx_info->skb == NULL))
			continue;

		hdr = (struct ieee80211_hdr *)tx_info->skb->data;
		if (ieee80211_is_data_qos(hdr->frame_control))
			nfreed++;

		il4965_tx_status(il, tx_info,
				 txq_id >= IL4965_FIRST_AMPDU_QUEUE);
		tx_info->skb = NULL;

		il->cfg->ops->lib->txq_free_tfd(il, txq);
	}
	return nfreed;
}

/**
 * il4965_tx_status_reply_compressed_ba - Update tx status from block-ack
 *
 * Go through block-ack's bitmap of ACK'd frames, update driver's record of
 * ACK vs. not.  This gets sent to mac80211, then to rate scaling algo.
 */
static int il4965_tx_status_reply_compressed_ba(struct il_priv *il,
				 struct il_ht_agg *agg,
				 struct il_compressed_ba_resp *ba_resp)

{
	int i, sh, ack;
	u16 seq_ctl = le16_to_cpu(ba_resp->seq_ctl);
	u16 scd_flow = le16_to_cpu(ba_resp->scd_flow);
	int successes = 0;
	struct ieee80211_tx_info *info;
	u64 bitmap, sent_bitmap;

	if (unlikely(!agg->wait_for_ba))  {
		if (unlikely(ba_resp->bitmap))
			IL_ERR("Received BA when not expected\n");
		return -EINVAL;
	}

	/* Mark that the expected block-ack response arrived */
	agg->wait_for_ba = 0;
	D_TX_REPLY("BA %d %d\n", agg->start_idx,
							ba_resp->seq_ctl);

	/* Calculate shift to align block-ack bits with our Tx window bits */
	sh = agg->start_idx - SEQ_TO_INDEX(seq_ctl >> 4);
	if (sh < 0) /* tbw something is wrong with indices */
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
		D_TX_REPLY("%s ON i=%d idx=%d raw=%d\n",
			ack ? "ACK" : "NACK", i,
			(agg->start_idx + i) & 0xff,
			agg->start_idx + i);
		sent_bitmap >>= 1;
		++i;
	}

	D_TX_REPLY("Bitmap %llx\n",
				   (unsigned long long)bitmap);

	info = IEEE80211_SKB_CB(il->txq[scd_flow].txb[agg->start_idx].skb);
	memset(&info->status, 0, sizeof(info->status));
	info->flags |= IEEE80211_TX_STAT_ACK;
	info->flags |= IEEE80211_TX_STAT_AMPDU;
	info->status.ampdu_ack_len = successes;
	info->status.ampdu_len = agg->frame_count;
	il4965_hwrate_to_tx_control(il, agg->rate_n_flags, info);

	return 0;
}

/**
 * translate ucode response to mac80211 tx status control values
 */
void il4965_hwrate_to_tx_control(struct il_priv *il, u32 rate_n_flags,
				  struct ieee80211_tx_info *info)
{
	struct ieee80211_tx_rate *r = &info->control.rates[0];

	info->antenna_sel_tx =
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

/**
 * il4965_rx_reply_compressed_ba - Handler for REPLY_COMPRESSED_BA
 *
 * Handles block-acknowledge notification from device, which reports success
 * of frames sent via aggregation.
 */
void il4965_rx_reply_compressed_ba(struct il_priv *il,
					   struct il_rx_mem_buffer *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_compressed_ba_resp *ba_resp = &pkt->u.compressed_ba;
	struct il_tx_queue *txq = NULL;
	struct il_ht_agg *agg;
	int index;
	int sta_id;
	int tid;
	unsigned long flags;

	/* "flow" corresponds to Tx queue */
	u16 scd_flow = le16_to_cpu(ba_resp->scd_flow);

	/* "ssn" is start of block-ack Tx window, corresponds to index
	 * (in Tx queue's circular buffer) of first TFD/frame in window */
	u16 ba_resp_scd_ssn = le16_to_cpu(ba_resp->scd_ssn);

	if (scd_flow >= il->hw_params.max_txq_num) {
		IL_ERR(
			"BUG_ON scd_flow is bigger than number of queues\n");
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
		D_TX_REPLY(
			"BA scd_flow %d does not match txq_id %d\n",
			scd_flow, agg->txq_id);
		return;
	}

	/* Find index just before block-ack window */
	index = il_queue_dec_wrap(ba_resp_scd_ssn & 0xff, txq->q.n_bd);

	spin_lock_irqsave(&il->sta_lock, flags);

	D_TX_REPLY("REPLY_COMPRESSED_BA [%d] Received from %pM, "
			   "sta_id = %d\n",
			   agg->wait_for_ba,
			   (u8 *) &ba_resp->sta_addr_lo32,
			   ba_resp->sta_id);
	D_TX_REPLY("TID = %d, SeqCtl = %d, bitmap = 0x%llx,"
			"scd_flow = "
			   "%d, scd_ssn = %d\n",
			   ba_resp->tid,
			   ba_resp->seq_ctl,
			   (unsigned long long)le64_to_cpu(ba_resp->bitmap),
			   ba_resp->scd_flow,
			   ba_resp->scd_ssn);
	D_TX_REPLY("DAT start_idx = %d, bitmap = 0x%llx\n",
			   agg->start_idx,
			   (unsigned long long)agg->bitmap);

	/* Update driver's record of ACK vs. not for each frame in window */
	il4965_tx_status_reply_compressed_ba(il, agg, ba_resp);

	/* Release all TFDs before the SSN, i.e. all TFDs in front of
	 * block-ack window (we assume that they've been successfully
	 * transmitted ... if not, it's too late anyway). */
	if (txq->q.read_ptr != (ba_resp_scd_ssn & 0xff)) {
		/* calculate mac80211 ampdu sw queue to wake */
		int freed = il4965_tx_queue_reclaim(il, scd_flow, index);
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
const char *il4965_get_tx_fail_reason(u32 status)
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
