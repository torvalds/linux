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

#include <net/ieee80211_radiotap.h>
#include <net/mac80211.h>

#include <asm/div64.h>

#define DRV_NAME	"iwl3945"

#include "iwl-fh.h"
#include "iwl-3945-fh.h"
#include "iwl-commands.h"
#include "iwl-sta.h"
#include "iwl-3945.h"
#include "iwl-core.h"
#include "iwl-helpers.h"
#include "iwl-dev.h"
#include "iwl-spectrum.h"

/*
 * module name, copyright, version, etc.
 */

#define DRV_DESCRIPTION	\
"Intel(R) PRO/Wireless 3945ABG/BG Network Connection driver for Linux"

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
#define VD "d"
#else
#define VD
#endif

/*
 * add "s" to indicate spectrum measurement included.
 * we add it here to be consistent with previous releases in which
 * this was configurable.
 */
#define DRV_VERSION  IWLWIFI_VERSION VD "s"
#define DRV_COPYRIGHT	"Copyright(c) 2003-2011 Intel Corporation"
#define DRV_AUTHOR     "<ilw@linux.intel.com>"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");

 /* module parameters */
struct iwl_mod_params iwl3945_mod_params = {
	.sw_crypto = 1,
	.restart_fw = 1,
	.disable_hw_scan = 1,
	/* the rest are 0 by default */
};

/**
 * iwl3945_get_antenna_flags - Get antenna flags for RXON command
 * @priv: eeprom and antenna fields are used to determine antenna flags
 *
 * priv->eeprom39  is used to determine if antenna AUX/MAIN are reversed
 * iwl3945_mod_params.antenna specifies the antenna diversity mode:
 *
 * IWL_ANTENNA_DIVERSITY - NIC selects best antenna by itself
 * IWL_ANTENNA_MAIN      - Force MAIN antenna
 * IWL_ANTENNA_AUX       - Force AUX antenna
 */
__le32 iwl3945_get_antenna_flags(const struct iwl_priv *priv)
{
	struct iwl3945_eeprom *eeprom = (struct iwl3945_eeprom *)priv->eeprom;

	switch (iwl3945_mod_params.antenna) {
	case IWL_ANTENNA_DIVERSITY:
		return 0;

	case IWL_ANTENNA_MAIN:
		if (eeprom->antenna_switch_type)
			return RXON_FLG_DIS_DIV_MSK | RXON_FLG_ANT_B_MSK;
		return RXON_FLG_DIS_DIV_MSK | RXON_FLG_ANT_A_MSK;

	case IWL_ANTENNA_AUX:
		if (eeprom->antenna_switch_type)
			return RXON_FLG_DIS_DIV_MSK | RXON_FLG_ANT_A_MSK;
		return RXON_FLG_DIS_DIV_MSK | RXON_FLG_ANT_B_MSK;
	}

	/* bad antenna selector value */
	IWL_ERR(priv, "Bad antenna selector value (0x%x)\n",
		iwl3945_mod_params.antenna);

	return 0;		/* "diversity" is default if error */
}

static int iwl3945_set_ccmp_dynamic_key_info(struct iwl_priv *priv,
				   struct ieee80211_key_conf *keyconf,
				   u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	int ret;

	key_flags |= (STA_KEY_FLG_CCMP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);

	if (sta_id == priv->contexts[IWL_RXON_CTX_BSS].bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->hw_key_idx = keyconf->keyidx;
	key_flags &= ~STA_KEY_FLG_INVALID;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].keyinfo.cipher = keyconf->cipher;
	priv->stations[sta_id].keyinfo.keylen = keyconf->keylen;
	memcpy(priv->stations[sta_id].keyinfo.key, keyconf->key,
	       keyconf->keylen);

	memcpy(priv->stations[sta_id].sta.key.key, keyconf->key,
	       keyconf->keylen);

	if ((priv->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		priv->stations[sta_id].sta.key.key_offset =
				 iwl_legacy_get_free_ucode_key_index(priv);
	/* else, we are overriding an existing key => no need to allocated room
	* in uCode. */

	WARN(priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	priv->stations[sta_id].sta.key.key_flags = key_flags;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	IWL_DEBUG_INFO(priv, "hwcrypto: modify ucode station key info\n");

	ret = iwl_legacy_send_add_sta(priv,
				&priv->stations[sta_id].sta, CMD_ASYNC);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}

static int iwl3945_set_tkip_dynamic_key_info(struct iwl_priv *priv,
				  struct ieee80211_key_conf *keyconf,
				  u8 sta_id)
{
	return -EOPNOTSUPP;
}

static int iwl3945_set_wep_dynamic_key_info(struct iwl_priv *priv,
				  struct ieee80211_key_conf *keyconf,
				  u8 sta_id)
{
	return -EOPNOTSUPP;
}

static int iwl3945_clear_sta_key_info(struct iwl_priv *priv, u8 sta_id)
{
	unsigned long flags;
	struct iwl_legacy_addsta_cmd sta_cmd;

	spin_lock_irqsave(&priv->sta_lock, flags);
	memset(&priv->stations[sta_id].keyinfo, 0, sizeof(struct iwl_hw_key));
	memset(&priv->stations[sta_id].sta.key, 0,
		sizeof(struct iwl4965_keyinfo));
	priv->stations[sta_id].sta.key.key_flags = STA_KEY_FLG_NO_ENC;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_legacy_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	IWL_DEBUG_INFO(priv, "hwcrypto: clear ucode station key info\n");
	return iwl_legacy_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}

static int iwl3945_set_dynamic_key(struct iwl_priv *priv,
			struct ieee80211_key_conf *keyconf, u8 sta_id)
{
	int ret = 0;

	keyconf->hw_key_idx = HW_KEY_DYNAMIC;

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		ret = iwl3945_set_ccmp_dynamic_key_info(priv, keyconf, sta_id);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		ret = iwl3945_set_tkip_dynamic_key_info(priv, keyconf, sta_id);
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		ret = iwl3945_set_wep_dynamic_key_info(priv, keyconf, sta_id);
		break;
	default:
		IWL_ERR(priv, "Unknown alg: %s alg=%x\n", __func__,
			keyconf->cipher);
		ret = -EINVAL;
	}

	IWL_DEBUG_WEP(priv, "Set dynamic key: alg=%x len=%d idx=%d sta=%d ret=%d\n",
		      keyconf->cipher, keyconf->keylen, keyconf->keyidx,
		      sta_id, ret);

	return ret;
}

static int iwl3945_remove_static_key(struct iwl_priv *priv)
{
	int ret = -EOPNOTSUPP;

	return ret;
}

static int iwl3945_set_static_key(struct iwl_priv *priv,
				struct ieee80211_key_conf *key)
{
	if (key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	    key->cipher == WLAN_CIPHER_SUITE_WEP104)
		return -EOPNOTSUPP;

	IWL_ERR(priv, "Static key invalid: cipher %x\n", key->cipher);
	return -EINVAL;
}

static void iwl3945_clear_free_frames(struct iwl_priv *priv)
{
	struct list_head *element;

	IWL_DEBUG_INFO(priv, "%d frames on pre-allocated heap on clear.\n",
		       priv->frames_count);

	while (!list_empty(&priv->free_frames)) {
		element = priv->free_frames.next;
		list_del(element);
		kfree(list_entry(element, struct iwl3945_frame, list));
		priv->frames_count--;
	}

	if (priv->frames_count) {
		IWL_WARN(priv, "%d frames still in use.  Did we lose one?\n",
			    priv->frames_count);
		priv->frames_count = 0;
	}
}

static struct iwl3945_frame *iwl3945_get_free_frame(struct iwl_priv *priv)
{
	struct iwl3945_frame *frame;
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
	return list_entry(element, struct iwl3945_frame, list);
}

static void iwl3945_free_frame(struct iwl_priv *priv, struct iwl3945_frame *frame)
{
	memset(frame, 0, sizeof(*frame));
	list_add(&frame->list, &priv->free_frames);
}

unsigned int iwl3945_fill_beacon_frame(struct iwl_priv *priv,
				struct ieee80211_hdr *hdr,
				int left)
{

	if (!iwl_legacy_is_associated(priv, IWL_RXON_CTX_BSS) || !priv->beacon_skb)
		return 0;

	if (priv->beacon_skb->len > left)
		return 0;

	memcpy(hdr, priv->beacon_skb->data, priv->beacon_skb->len);

	return priv->beacon_skb->len;
}

static int iwl3945_send_beacon_cmd(struct iwl_priv *priv)
{
	struct iwl3945_frame *frame;
	unsigned int frame_size;
	int rc;
	u8 rate;

	frame = iwl3945_get_free_frame(priv);

	if (!frame) {
		IWL_ERR(priv, "Could not obtain free frame buffer for beacon "
			  "command.\n");
		return -ENOMEM;
	}

	rate = iwl_legacy_get_lowest_plcp(priv,
				&priv->contexts[IWL_RXON_CTX_BSS]);

	frame_size = iwl3945_hw_get_beacon_cmd(priv, frame, rate);

	rc = iwl_legacy_send_cmd_pdu(priv, REPLY_TX_BEACON, frame_size,
			      &frame->u.cmd[0]);

	iwl3945_free_frame(priv, frame);

	return rc;
}

static void iwl3945_unset_hw_params(struct iwl_priv *priv)
{
	if (priv->_3945.shared_virt)
		dma_free_coherent(&priv->pci_dev->dev,
				  sizeof(struct iwl3945_shared),
				  priv->_3945.shared_virt,
				  priv->_3945.shared_phys);
}

static void iwl3945_build_tx_cmd_hwcrypto(struct iwl_priv *priv,
				      struct ieee80211_tx_info *info,
				      struct iwl_device_cmd *cmd,
				      struct sk_buff *skb_frag,
				      int sta_id)
{
	struct iwl3945_tx_cmd *tx_cmd = (struct iwl3945_tx_cmd *)cmd->cmd.payload;
	struct iwl_hw_key *keyinfo = &priv->stations[sta_id].keyinfo;

	tx_cmd->sec_ctl = 0;

	switch (keyinfo->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		tx_cmd->sec_ctl = TX_CMD_SEC_CCM;
		memcpy(tx_cmd->key, keyinfo->key, keyinfo->keylen);
		IWL_DEBUG_TX(priv, "tx_cmd with AES hwcrypto\n");
		break;

	case WLAN_CIPHER_SUITE_TKIP:
		break;

	case WLAN_CIPHER_SUITE_WEP104:
		tx_cmd->sec_ctl |= TX_CMD_SEC_KEY128;
		/* fall through */
	case WLAN_CIPHER_SUITE_WEP40:
		tx_cmd->sec_ctl |= TX_CMD_SEC_WEP |
		    (info->control.hw_key->hw_key_idx & TX_CMD_SEC_MSK) << TX_CMD_SEC_SHIFT;

		memcpy(&tx_cmd->key[3], keyinfo->key, keyinfo->keylen);

		IWL_DEBUG_TX(priv, "Configuring packet for WEP encryption "
			     "with key %d\n", info->control.hw_key->hw_key_idx);
		break;

	default:
		IWL_ERR(priv, "Unknown encode cipher %x\n", keyinfo->cipher);
		break;
	}
}

/*
 * handle build REPLY_TX command notification.
 */
static void iwl3945_build_tx_cmd_basic(struct iwl_priv *priv,
				  struct iwl_device_cmd *cmd,
				  struct ieee80211_tx_info *info,
				  struct ieee80211_hdr *hdr, u8 std_id)
{
	struct iwl3945_tx_cmd *tx_cmd = (struct iwl3945_tx_cmd *)cmd->cmd.payload;
	__le32 tx_flags = tx_cmd->tx_flags;
	__le16 fc = hdr->frame_control;

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

	iwl_legacy_tx_cmd_protection(priv, info, fc, &tx_flags);

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

/*
 * start REPLY_TX command process
 */
static int iwl3945_tx_skb(struct iwl_priv *priv, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct iwl3945_tx_cmd *tx_cmd;
	struct iwl_tx_queue *txq = NULL;
	struct iwl_queue *q = NULL;
	struct iwl_device_cmd *out_cmd;
	struct iwl_cmd_meta *out_meta;
	dma_addr_t phys_addr;
	dma_addr_t txcmd_phys;
	int txq_id = skb_get_queue_mapping(skb);
	u16 len, idx, hdr_len;
	u8 id;
	u8 unicast;
	u8 sta_id;
	u8 tid = 0;
	__le16 fc;
	u8 wait_write_ptr = 0;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (iwl_legacy_is_rfkill(priv)) {
		IWL_DEBUG_DROP(priv, "Dropping - RF KILL\n");
		goto drop_unlock;
	}

	if ((ieee80211_get_tx_rate(priv->hw, info)->hw_value & 0xFF) == IWL_INVALID_RATE) {
		IWL_ERR(priv, "ERROR: No TX rate available.\n");
		goto drop_unlock;
	}

	unicast = !is_multicast_ether_addr(hdr->addr1);
	id = 0;

	fc = hdr->frame_control;

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	if (ieee80211_is_auth(fc))
		IWL_DEBUG_TX(priv, "Sending AUTH frame\n");
	else if (ieee80211_is_assoc_req(fc))
		IWL_DEBUG_TX(priv, "Sending ASSOC frame\n");
	else if (ieee80211_is_reassoc_req(fc))
		IWL_DEBUG_TX(priv, "Sending REASSOC frame\n");
#endif

	spin_unlock_irqrestore(&priv->lock, flags);

	hdr_len = ieee80211_hdrlen(fc);

	/* Find index into station table for destination station */
	sta_id = iwl_legacy_sta_id_or_broadcast(
			priv, &priv->contexts[IWL_RXON_CTX_BSS],
			info->control.sta);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_DEBUG_DROP(priv, "Dropping - INVALID STATION: %pM\n",
			       hdr->addr1);
		goto drop;
	}

	IWL_DEBUG_RATE(priv, "station Id %d\n", sta_id);

	if (ieee80211_is_data_qos(fc)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
		if (unlikely(tid >= MAX_TID_COUNT))
			goto drop;
	}

	/* Descriptor for chosen Tx queue */
	txq = &priv->txq[txq_id];
	q = &txq->q;

	if ((iwl_legacy_queue_space(q) < q->high_mark))
		goto drop;

	spin_lock_irqsave(&priv->lock, flags);

	idx = iwl_legacy_get_cmd_index(q, q->write_ptr, 0);

	/* Set up driver data for this TFD */
	memset(&(txq->txb[q->write_ptr]), 0, sizeof(struct iwl_tx_info));
	txq->txb[q->write_ptr].skb = skb;
	txq->txb[q->write_ptr].ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	/* Init first empty entry in queue's array of Tx/cmd buffers */
	out_cmd = txq->cmd[idx];
	out_meta = &txq->meta[idx];
	tx_cmd = (struct iwl3945_tx_cmd *)out_cmd->cmd.payload;
	memset(&out_cmd->hdr, 0, sizeof(out_cmd->hdr));
	memset(tx_cmd, 0, sizeof(*tx_cmd));

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


	if (info->control.hw_key)
		iwl3945_build_tx_cmd_hwcrypto(priv, info, out_cmd, skb, sta_id);

	/* TODO need this for burst mode later on */
	iwl3945_build_tx_cmd_basic(priv, out_cmd, info, hdr, sta_id);

	/* set is_hcca to 0; it probably will never be implemented */
	iwl3945_hw_build_tx_cmd_rate(priv, out_cmd, info, hdr, sta_id, 0);

	/* Total # bytes to be transmitted */
	len = (u16)skb->len;
	tx_cmd->len = cpu_to_le16(len);

	iwl_legacy_dbg_log_tx_data_frame(priv, len, hdr);
	iwl_legacy_update_stats(priv, true, fc, len);
	tx_cmd->tx_flags &= ~TX_CMD_FLG_ANT_A_MSK;
	tx_cmd->tx_flags &= ~TX_CMD_FLG_ANT_B_MSK;

	if (!ieee80211_has_morefrags(hdr->frame_control)) {
		txq->need_update = 1;
	} else {
		wait_write_ptr = 1;
		txq->need_update = 0;
	}

	IWL_DEBUG_TX(priv, "sequence nr = 0X%x\n",
		     le16_to_cpu(out_cmd->hdr.sequence));
	IWL_DEBUG_TX(priv, "tx_flags = 0X%x\n", le32_to_cpu(tx_cmd->tx_flags));
	iwl_print_hex_dump(priv, IWL_DL_TX, tx_cmd, sizeof(*tx_cmd));
	iwl_print_hex_dump(priv, IWL_DL_TX, (u8 *)tx_cmd->hdr,
			   ieee80211_hdrlen(fc));

	/*
	 * Use the first empty entry in this queue's command buffer array
	 * to contain the Tx command and MAC header concatenated together
	 * (payload data will be in another buffer).
	 * Size of this varies, due to varying MAC header length.
	 * If end is not dword aligned, we'll have 2 extra bytes at the end
	 * of the MAC header (device reads on dword boundaries).
	 * We'll tell device about this padding later.
	 */
	len = sizeof(struct iwl3945_tx_cmd) +
			sizeof(struct iwl_cmd_header) + hdr_len;
	len = (len + 3) & ~3;

	/* Physical address of this Tx command's header (not MAC header!),
	 * within command buffer array. */
	txcmd_phys = pci_map_single(priv->pci_dev, &out_cmd->hdr,
				    len, PCI_DMA_TODEVICE);
	/* we do not map meta data ... so we can safely access address to
	 * provide to unmap command*/
	dma_unmap_addr_set(out_meta, mapping, txcmd_phys);
	dma_unmap_len_set(out_meta, len, len);

	/* Add buffer containing Tx command and MAC(!) header to TFD's
	 * first entry */
	priv->cfg->ops->lib->txq_attach_buf_to_tfd(priv, txq,
						   txcmd_phys, len, 1, 0);


	/* Set up TFD's 2nd entry to point directly to remainder of skb,
	 * if any (802.11 null frames have no payload). */
	len = skb->len - hdr_len;
	if (len) {
		phys_addr = pci_map_single(priv->pci_dev, skb->data + hdr_len,
					   len, PCI_DMA_TODEVICE);
		priv->cfg->ops->lib->txq_attach_buf_to_tfd(priv, txq,
							   phys_addr, len,
							   0, U32_PAD(len));
	}


	/* Tell device the write index *just past* this latest filled TFD */
	q->write_ptr = iwl_legacy_queue_inc_wrap(q->write_ptr, q->n_bd);
	iwl_legacy_txq_update_write_ptr(priv, txq);
	spin_unlock_irqrestore(&priv->lock, flags);

	if ((iwl_legacy_queue_space(q) < q->high_mark)
	    && priv->mac80211_registered) {
		if (wait_write_ptr) {
			spin_lock_irqsave(&priv->lock, flags);
			txq->need_update = 1;
			iwl_legacy_txq_update_write_ptr(priv, txq);
			spin_unlock_irqrestore(&priv->lock, flags);
		}

		iwl_legacy_stop_queue(priv, txq);
	}

	return 0;

drop_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
drop:
	return -1;
}

static int iwl3945_get_measurement(struct iwl_priv *priv,
			       struct ieee80211_measurement_params *params,
			       u8 type)
{
	struct iwl_spectrum_cmd spectrum;
	struct iwl_rx_packet *pkt;
	struct iwl_host_cmd cmd = {
		.id = REPLY_SPECTRUM_MEASUREMENT_CMD,
		.data = (void *)&spectrum,
		.flags = CMD_WANT_SKB,
	};
	u32 add_time = le64_to_cpu(params->start_time);
	int rc;
	int spectrum_resp_status;
	int duration = le16_to_cpu(params->duration);
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	if (iwl_legacy_is_associated(priv, IWL_RXON_CTX_BSS))
		add_time = iwl_legacy_usecs_to_beacons(priv,
			le64_to_cpu(params->start_time) - priv->_3945.last_tsf,
			le16_to_cpu(ctx->timing.beacon_interval));

	memset(&spectrum, 0, sizeof(spectrum));

	spectrum.channel_count = cpu_to_le16(1);
	spectrum.flags =
	    RXON_FLG_TSF2HOST_MSK | RXON_FLG_ANT_A_MSK | RXON_FLG_DIS_DIV_MSK;
	spectrum.filter_flags = MEASUREMENT_FILTER_FLAG;
	cmd.len = sizeof(spectrum);
	spectrum.len = cpu_to_le16(cmd.len - sizeof(spectrum.len));

	if (iwl_legacy_is_associated(priv, IWL_RXON_CTX_BSS))
		spectrum.start_time =
			iwl_legacy_add_beacon_time(priv,
				priv->_3945.last_beacon_time, add_time,
				le16_to_cpu(ctx->timing.beacon_interval));
	else
		spectrum.start_time = 0;

	spectrum.channels[0].duration = cpu_to_le32(duration * TIME_UNIT);
	spectrum.channels[0].channel = params->channel;
	spectrum.channels[0].type = type;
	if (ctx->active.flags & RXON_FLG_BAND_24G_MSK)
		spectrum.flags |= RXON_FLG_BAND_24G_MSK |
		    RXON_FLG_AUTO_DETECT_MSK | RXON_FLG_TGG_PROTECT_MSK;

	rc = iwl_legacy_send_cmd_sync(priv, &cmd);
	if (rc)
		return rc;

	pkt = (struct iwl_rx_packet *)cmd.reply_page;
	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from REPLY_RX_ON_ASSOC command\n");
		rc = -EIO;
	}

	spectrum_resp_status = le16_to_cpu(pkt->u.spectrum.status);
	switch (spectrum_resp_status) {
	case 0:		/* Command will be handled */
		if (pkt->u.spectrum.id != 0xff) {
			IWL_DEBUG_INFO(priv, "Replaced existing measurement: %d\n",
						pkt->u.spectrum.id);
			priv->measurement_status &= ~MEASUREMENT_READY;
		}
		priv->measurement_status |= MEASUREMENT_ACTIVE;
		rc = 0;
		break;

	case 1:		/* Command will not be handled */
		rc = -EAGAIN;
		break;
	}

	iwl_legacy_free_pages(priv, cmd.reply_page);

	return rc;
}

static void iwl3945_rx_reply_alive(struct iwl_priv *priv,
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
		memcpy(&priv->card_alive_init, &pkt->u.alive_frame,
		       sizeof(struct iwl_alive_resp));
		pwork = &priv->init_alive_start;
	} else {
		IWL_DEBUG_INFO(priv, "Runtime Alive received.\n");
		memcpy(&priv->card_alive, &pkt->u.alive_frame,
		       sizeof(struct iwl_alive_resp));
		pwork = &priv->alive_start;
		iwl3945_disable_events(priv);
	}

	/* We delay the ALIVE response by 5ms to
	 * give the HW RF Kill time to activate... */
	if (palive->is_valid == UCODE_VALID_OK)
		queue_delayed_work(priv->workqueue, pwork,
				   msecs_to_jiffies(5));
	else
		IWL_WARN(priv, "uCode did not respond OK.\n");
}

static void iwl3945_rx_reply_add_sta(struct iwl_priv *priv,
				 struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
#endif

	IWL_DEBUG_RX(priv, "Received REPLY_ADD_STA: 0x%02X\n", pkt->u.status);
}

static void iwl3945_rx_beacon_notif(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl3945_beacon_notif *beacon = &(pkt->u.beacon_status);
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	u8 rate = beacon->beacon_notify_hdr.rate;

	IWL_DEBUG_RX(priv, "beacon status %x retries %d iss %d "
		"tsf %d %d rate %d\n",
		le32_to_cpu(beacon->beacon_notify_hdr.status) & TX_STATUS_MSK,
		beacon->beacon_notify_hdr.failure_frame,
		le32_to_cpu(beacon->ibss_mgr_status),
		le32_to_cpu(beacon->high_tsf),
		le32_to_cpu(beacon->low_tsf), rate);
#endif

	priv->ibss_manager = le32_to_cpu(beacon->ibss_mgr_status);

}

/* Handle notification from uCode that card's power state is changing
 * due to software, hardware, or critical temperature RFKILL */
static void iwl3945_rx_card_state_notif(struct iwl_priv *priv,
				    struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u32 flags = le32_to_cpu(pkt->u.card_state_notif.flags);
	unsigned long status = priv->status;

	IWL_WARN(priv, "Card state received: HW:%s SW:%s\n",
			  (flags & HW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & SW_CARD_DISABLED) ? "Kill" : "On");

	iwl_write32(priv, CSR_UCODE_DRV_GP1_SET,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	if (flags & HW_CARD_DISABLED)
		set_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		clear_bit(STATUS_RF_KILL_HW, &priv->status);


	iwl_legacy_scan_cancel(priv);

	if ((test_bit(STATUS_RF_KILL_HW, &status) !=
	     test_bit(STATUS_RF_KILL_HW, &priv->status)))
		wiphy_rfkill_set_hw_state(priv->hw->wiphy,
				test_bit(STATUS_RF_KILL_HW, &priv->status));
	else
		wake_up(&priv->wait_command_queue);
}

/**
 * iwl3945_setup_rx_handlers - Initialize Rx handler callbacks
 *
 * Setup the RX handlers for each of the reply types sent from the uCode
 * to the host.
 *
 * This function chains into the hardware specific files for them to setup
 * any hardware specific handlers as well.
 */
static void iwl3945_setup_rx_handlers(struct iwl_priv *priv)
{
	priv->rx_handlers[REPLY_ALIVE] = iwl3945_rx_reply_alive;
	priv->rx_handlers[REPLY_ADD_STA] = iwl3945_rx_reply_add_sta;
	priv->rx_handlers[REPLY_ERROR] = iwl_legacy_rx_reply_error;
	priv->rx_handlers[CHANNEL_SWITCH_NOTIFICATION] = iwl_legacy_rx_csa;
	priv->rx_handlers[SPECTRUM_MEASURE_NOTIFICATION] =
			iwl_legacy_rx_spectrum_measure_notif;
	priv->rx_handlers[PM_SLEEP_NOTIFICATION] = iwl_legacy_rx_pm_sleep_notif;
	priv->rx_handlers[PM_DEBUG_STATISTIC_NOTIFIC] =
	    iwl_legacy_rx_pm_debug_statistics_notif;
	priv->rx_handlers[BEACON_NOTIFICATION] = iwl3945_rx_beacon_notif;

	/*
	 * The same handler is used for both the REPLY to a discrete
	 * statistics request from the host as well as for the periodic
	 * statistics notifications (after received beacons) from the uCode.
	 */
	priv->rx_handlers[REPLY_STATISTICS_CMD] = iwl3945_reply_statistics;
	priv->rx_handlers[STATISTICS_NOTIFICATION] = iwl3945_hw_rx_statistics;

	iwl_legacy_setup_rx_scan_handlers(priv);
	priv->rx_handlers[CARD_STATE_NOTIFICATION] = iwl3945_rx_card_state_notif;

	/* Set up hardware specific Rx handlers */
	iwl3945_hw_rx_handler_setup(priv);
}

/************************** RX-FUNCTIONS ****************************/
/*
 * Rx theory of operation
 *
 * The host allocates 32 DMA target addresses and passes the host address
 * to the firmware at register IWL_RFDS_TABLE_LOWER + N * RFD_SIZE where N is
 * 0 to 31
 *
 * Rx Queue Indexes
 * The host/firmware share two index registers for managing the Rx buffers.
 *
 * The READ index maps to the first position that the firmware may be writing
 * to -- the driver can read up to (but not including) this position and get
 * good data.
 * The READ index is managed by the firmware once the card is enabled.
 *
 * The WRITE index maps to the last position the driver has read from -- the
 * position preceding WRITE is the last slot the firmware can place a packet.
 *
 * The queue is empty (no good data) if WRITE = READ - 1, and is full if
 * WRITE = READ.
 *
 * During initialization, the host sets up the READ queue position to the first
 * INDEX position, and WRITE to the last (READ - 1 wrapped)
 *
 * When the firmware places a packet in a buffer, it will advance the READ index
 * and fire the RX interrupt.  The driver can then query the READ index and
 * process as many packets as possible, moving the WRITE index forward as it
 * resets the Rx queue buffers with new memory.
 *
 * The management in the driver is as follows:
 * + A list of pre-allocated SKBs is stored in iwl->rxq->rx_free.  When
 *   iwl->rxq->free_count drops to or below RX_LOW_WATERMARK, work is scheduled
 *   to replenish the iwl->rxq->rx_free.
 * + In iwl3945_rx_replenish (scheduled) if 'processed' != 'read' then the
 *   iwl->rxq is replenished and the READ INDEX is updated (updating the
 *   'processed' and 'read' driver indexes as well)
 * + A received packet is processed and handed to the kernel network stack,
 *   detached from the iwl->rxq.  The driver 'processed' index is updated.
 * + The Host/Firmware iwl->rxq is replenished at tasklet time from the rx_free
 *   list. If there are no allocated buffers in iwl->rxq->rx_free, the READ
 *   INDEX is not incremented and iwl->status(RX_STALLED) is set.  If there
 *   were enough free buffers and RX_STALLED is set it is cleared.
 *
 *
 * Driver sequence:
 *
 * iwl3945_rx_replenish()     Replenishes rx_free list from rx_used, and calls
 *                            iwl3945_rx_queue_restock
 * iwl3945_rx_queue_restock() Moves available buffers from rx_free into Rx
 *                            queue, updates firmware pointers, and updates
 *                            the WRITE index.  If insufficient rx_free buffers
 *                            are available, schedules iwl3945_rx_replenish
 *
 * -- enable interrupts --
 * ISR - iwl3945_rx()         Detach iwl_rx_mem_buffers from pool up to the
 *                            READ INDEX, detaching the SKB from the pool.
 *                            Moves the packet buffer from queue to rx_used.
 *                            Calls iwl3945_rx_queue_restock to refill any empty
 *                            slots.
 * ...
 *
 */

/**
 * iwl3945_dma_addr2rbd_ptr - convert a DMA address to a uCode read buffer ptr
 */
static inline __le32 iwl3945_dma_addr2rbd_ptr(struct iwl_priv *priv,
					  dma_addr_t dma_addr)
{
	return cpu_to_le32((u32)dma_addr);
}

/**
 * iwl3945_rx_queue_restock - refill RX queue from pre-allocated pool
 *
 * If there are slots in the RX queue that need to be restocked,
 * and we have free pre-allocated buffers, fill the ranks as much
 * as we can, pulling from rx_free.
 *
 * This moves the 'write' index forward to catch up with 'processed', and
 * also updates the memory address in the firmware to reference the new
 * target buffer.
 */
static void iwl3945_rx_queue_restock(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	struct list_head *element;
	struct iwl_rx_mem_buffer *rxb;
	unsigned long flags;
	int write;

	spin_lock_irqsave(&rxq->lock, flags);
	write = rxq->write & ~0x7;
	while ((iwl_legacy_rx_queue_space(rxq) > 0) && (rxq->free_count)) {
		/* Get next free Rx buffer, remove from free list */
		element = rxq->rx_free.next;
		rxb = list_entry(element, struct iwl_rx_mem_buffer, list);
		list_del(element);

		/* Point to Rx buffer via next RBD in circular buffer */
		rxq->bd[rxq->write] = iwl3945_dma_addr2rbd_ptr(priv, rxb->page_dma);
		rxq->queue[rxq->write] = rxb;
		rxq->write = (rxq->write + 1) & RX_QUEUE_MASK;
		rxq->free_count--;
	}
	spin_unlock_irqrestore(&rxq->lock, flags);
	/* If the pre-allocated buffer pool is dropping low, schedule to
	 * refill it */
	if (rxq->free_count <= RX_LOW_WATERMARK)
		queue_work(priv->workqueue, &priv->rx_replenish);


	/* If we've added more space for the firmware to place data, tell it.
	 * Increment device's write pointer in multiples of 8. */
	if ((rxq->write_actual != (rxq->write & ~0x7))
	    || (abs(rxq->write - rxq->read) > 7)) {
		spin_lock_irqsave(&rxq->lock, flags);
		rxq->need_update = 1;
		spin_unlock_irqrestore(&rxq->lock, flags);
		iwl_legacy_rx_queue_update_write_ptr(priv, rxq);
	}
}

/**
 * iwl3945_rx_replenish - Move all used packet from rx_used to rx_free
 *
 * When moving to rx_free an SKB is allocated for the slot.
 *
 * Also restock the Rx queue via iwl3945_rx_queue_restock.
 * This is called as a scheduled work item (except for during initialization)
 */
static void iwl3945_rx_allocate(struct iwl_priv *priv, gfp_t priority)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	struct list_head *element;
	struct iwl_rx_mem_buffer *rxb;
	struct page *page;
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

		if (priv->hw_params.rx_page_order > 0)
			gfp_mask |= __GFP_COMP;

		/* Alloc a new receive buffer */
		page = alloc_pages(gfp_mask, priv->hw_params.rx_page_order);
		if (!page) {
			if (net_ratelimit())
				IWL_DEBUG_INFO(priv, "Failed to allocate SKB buffer.\n");
			if ((rxq->free_count <= RX_LOW_WATERMARK) &&
			    net_ratelimit())
				IWL_CRIT(priv, "Failed to allocate SKB buffer with %s. Only %u free buffers remaining.\n",
					 priority == GFP_ATOMIC ?  "GFP_ATOMIC" : "GFP_KERNEL",
					 rxq->free_count);
			/* We don't reschedule replenish work here -- we will
			 * call the restock method and if it still needs
			 * more buffers it will schedule replenish */
			break;
		}

		spin_lock_irqsave(&rxq->lock, flags);
		if (list_empty(&rxq->rx_used)) {
			spin_unlock_irqrestore(&rxq->lock, flags);
			__free_pages(page, priv->hw_params.rx_page_order);
			return;
		}
		element = rxq->rx_used.next;
		rxb = list_entry(element, struct iwl_rx_mem_buffer, list);
		list_del(element);
		spin_unlock_irqrestore(&rxq->lock, flags);

		rxb->page = page;
		/* Get physical address of RB/SKB */
		rxb->page_dma = pci_map_page(priv->pci_dev, page, 0,
				PAGE_SIZE << priv->hw_params.rx_page_order,
				PCI_DMA_FROMDEVICE);

		spin_lock_irqsave(&rxq->lock, flags);

		list_add_tail(&rxb->list, &rxq->rx_free);
		rxq->free_count++;
		priv->alloc_rxb_page++;

		spin_unlock_irqrestore(&rxq->lock, flags);
	}
}

void iwl3945_rx_queue_reset(struct iwl_priv *priv, struct iwl_rx_queue *rxq)
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
			pci_unmap_page(priv->pci_dev, rxq->pool[i].page_dma,
				PAGE_SIZE << priv->hw_params.rx_page_order,
				PCI_DMA_FROMDEVICE);
			__iwl_legacy_free_pages(priv, rxq->pool[i].page);
			rxq->pool[i].page = NULL;
		}
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);
	}

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->write_actual = 0;
	rxq->free_count = 0;
	spin_unlock_irqrestore(&rxq->lock, flags);
}

void iwl3945_rx_replenish(void *data)
{
	struct iwl_priv *priv = data;
	unsigned long flags;

	iwl3945_rx_allocate(priv, GFP_KERNEL);

	spin_lock_irqsave(&priv->lock, flags);
	iwl3945_rx_queue_restock(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void iwl3945_rx_replenish_now(struct iwl_priv *priv)
{
	iwl3945_rx_allocate(priv, GFP_ATOMIC);

	iwl3945_rx_queue_restock(priv);
}


/* Assumes that the skb field of the buffers in 'pool' is kept accurate.
 * If an SKB has been detached, the POOL needs to have its SKB set to NULL
 * This free routine walks the list of POOL entries and if SKB is set to
 * non NULL it is unmapped and freed
 */
static void iwl3945_rx_queue_free(struct iwl_priv *priv, struct iwl_rx_queue *rxq)
{
	int i;
	for (i = 0; i < RX_QUEUE_SIZE + RX_FREE_BUFFERS; i++) {
		if (rxq->pool[i].page != NULL) {
			pci_unmap_page(priv->pci_dev, rxq->pool[i].page_dma,
				PAGE_SIZE << priv->hw_params.rx_page_order,
				PCI_DMA_FROMDEVICE);
			__iwl_legacy_free_pages(priv, rxq->pool[i].page);
			rxq->pool[i].page = NULL;
		}
	}

	dma_free_coherent(&priv->pci_dev->dev, 4 * RX_QUEUE_SIZE, rxq->bd,
			  rxq->bd_dma);
	dma_free_coherent(&priv->pci_dev->dev, sizeof(struct iwl_rb_status),
			  rxq->rb_stts, rxq->rb_stts_dma);
	rxq->bd = NULL;
	rxq->rb_stts  = NULL;
}


/* Convert linear signal-to-noise ratio into dB */
static u8 ratio2dB[100] = {
/*	 0   1   2   3   4   5   6   7   8   9 */
	 0,  0,  6, 10, 12, 14, 16, 17, 18, 19, /* 00 - 09 */
	20, 21, 22, 22, 23, 23, 24, 25, 26, 26, /* 10 - 19 */
	26, 26, 26, 27, 27, 28, 28, 28, 29, 29, /* 20 - 29 */
	29, 30, 30, 30, 31, 31, 31, 31, 32, 32, /* 30 - 39 */
	32, 32, 32, 33, 33, 33, 33, 33, 34, 34, /* 40 - 49 */
	34, 34, 34, 34, 35, 35, 35, 35, 35, 35, /* 50 - 59 */
	36, 36, 36, 36, 36, 36, 36, 37, 37, 37, /* 60 - 69 */
	37, 37, 37, 37, 37, 38, 38, 38, 38, 38, /* 70 - 79 */
	38, 38, 38, 38, 38, 39, 39, 39, 39, 39, /* 80 - 89 */
	39, 39, 39, 39, 39, 40, 40, 40, 40, 40  /* 90 - 99 */
};

/* Calculates a relative dB value from a ratio of linear
 *   (i.e. not dB) signal levels.
 * Conversion assumes that levels are voltages (20*log), not powers (10*log). */
int iwl3945_calc_db_from_ratio(int sig_ratio)
{
	/* 1000:1 or higher just report as 60 dB */
	if (sig_ratio >= 1000)
		return 60;

	/* 100:1 or higher, divide by 10 and use table,
	 *   add 20 dB to make up for divide by 10 */
	if (sig_ratio >= 100)
		return 20 + (int)ratio2dB[sig_ratio/10];

	/* We shouldn't see this */
	if (sig_ratio < 1)
		return 0;

	/* Use table for ratios 1:1 - 99:1 */
	return (int)ratio2dB[sig_ratio];
}

/**
 * iwl3945_rx_handle - Main entry function for receiving responses from uCode
 *
 * Uses the priv->rx_handlers callback function array to invoke
 * the appropriate handlers, including command responses,
 * frame-received notifications, and other notifications.
 */
static void iwl3945_rx_handle(struct iwl_priv *priv)
{
	struct iwl_rx_mem_buffer *rxb;
	struct iwl_rx_packet *pkt;
	struct iwl_rx_queue *rxq = &priv->rxq;
	u32 r, i;
	int reclaim;
	unsigned long flags;
	u8 fill_rx = 0;
	u32 count = 8;
	int total_empty = 0;

	/* uCode's read index (stored in shared DRAM) indicates the last Rx
	 * buffer that the driver may process (last buffer filled by ucode). */
	r = le16_to_cpu(rxq->rb_stts->closed_rb_num) &  0x0FFF;
	i = rxq->read;

	/* calculate total frames need to be restock after handling RX */
	total_empty = r - rxq->write_actual;
	if (total_empty < 0)
		total_empty += RX_QUEUE_SIZE;

	if (total_empty > (RX_QUEUE_SIZE / 2))
		fill_rx = 1;
	/* Rx interrupt, but nothing sent from uCode */
	if (i == r)
		IWL_DEBUG_RX(priv, "r = %d, i = %d\n", r, i);

	while (i != r) {
		int len;

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

		len = le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;
		len += sizeof(u32); /* account for status word */
		trace_iwlwifi_legacy_dev_rx(priv, pkt, len);

		/* Reclaim a command buffer only if this packet is a response
		 *   to a (driver-originated) command.
		 * If the packet (e.g. Rx frame) originated from uCode,
		 *   there is no command buffer to reclaim.
		 * Ucode should set SEQ_RX_FRAME bit if ucode-originated,
		 *   but apparently a few don't get set; catch them here. */
		reclaim = !(pkt->hdr.sequence & SEQ_RX_FRAME) &&
			(pkt->hdr.cmd != STATISTICS_NOTIFICATION) &&
			(pkt->hdr.cmd != REPLY_TX);

		/* Based on type of command response or notification,
		 *   handle those that need handling via function in
		 *   rx_handlers table.  See iwl3945_setup_rx_handlers() */
		if (priv->rx_handlers[pkt->hdr.cmd]) {
			IWL_DEBUG_RX(priv, "r = %d, i = %d, %s, 0x%02x\n", r, i,
			iwl_legacy_get_cmd_string(pkt->hdr.cmd), pkt->hdr.cmd);
			priv->isr_stats.rx_handlers[pkt->hdr.cmd]++;
			priv->rx_handlers[pkt->hdr.cmd] (priv, rxb);
		} else {
			/* No handling needed */
			IWL_DEBUG_RX(priv,
				"r %d i %d No handler needed for %s, 0x%02x\n",
				r, i, iwl_legacy_get_cmd_string(pkt->hdr.cmd),
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
			 * and fire off the (possibly) blocking iwl_legacy_send_cmd()
			 * as we reclaim the driver command queue */
			if (rxb->page)
				iwl_legacy_tx_cmd_complete(priv, rxb);
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
		 * restock the Rx queue so ucode won't assert. */
		if (fill_rx) {
			count++;
			if (count >= 8) {
				rxq->read = i;
				iwl3945_rx_replenish_now(priv);
				count = 0;
			}
		}
	}

	/* Backtrack one entry */
	rxq->read = i;
	if (fill_rx)
		iwl3945_rx_replenish_now(priv);
	else
		iwl3945_rx_queue_restock(priv);
}

/* call this function to flush any scheduled tasklet */
static inline void iwl3945_synchronize_irq(struct iwl_priv *priv)
{
	/* wait to make sure we flush pending tasklet*/
	synchronize_irq(priv->pci_dev->irq);
	tasklet_kill(&priv->irq_tasklet);
}

static const char *iwl3945_desc_lookup(int i)
{
	switch (i) {
	case 1:
		return "FAIL";
	case 2:
		return "BAD_PARAM";
	case 3:
		return "BAD_CHECKSUM";
	case 4:
		return "NMI_INTERRUPT";
	case 5:
		return "SYSASSERT";
	case 6:
		return "FATAL_ERROR";
	}

	return "UNKNOWN";
}

#define ERROR_START_OFFSET  (1 * sizeof(u32))
#define ERROR_ELEM_SIZE     (7 * sizeof(u32))

void iwl3945_dump_nic_error_log(struct iwl_priv *priv)
{
	u32 i;
	u32 desc, time, count, base, data1;
	u32 blink1, blink2, ilink1, ilink2;

	base = le32_to_cpu(priv->card_alive.error_event_table_ptr);

	if (!iwl3945_hw_valid_rtc_data_addr(base)) {
		IWL_ERR(priv, "Not valid error log pointer 0x%08X\n", base);
		return;
	}


	count = iwl_legacy_read_targ_mem(priv, base);

	if (ERROR_START_OFFSET <= count * ERROR_ELEM_SIZE) {
		IWL_ERR(priv, "Start IWL Error Log Dump:\n");
		IWL_ERR(priv, "Status: 0x%08lX, count: %d\n",
			priv->status, count);
	}

	IWL_ERR(priv, "Desc       Time       asrtPC  blink2 "
		  "ilink1  nmiPC   Line\n");
	for (i = ERROR_START_OFFSET;
	     i < (count * ERROR_ELEM_SIZE) + ERROR_START_OFFSET;
	     i += ERROR_ELEM_SIZE) {
		desc = iwl_legacy_read_targ_mem(priv, base + i);
		time =
		    iwl_legacy_read_targ_mem(priv, base + i + 1 * sizeof(u32));
		blink1 =
		    iwl_legacy_read_targ_mem(priv, base + i + 2 * sizeof(u32));
		blink2 =
		    iwl_legacy_read_targ_mem(priv, base + i + 3 * sizeof(u32));
		ilink1 =
		    iwl_legacy_read_targ_mem(priv, base + i + 4 * sizeof(u32));
		ilink2 =
		    iwl_legacy_read_targ_mem(priv, base + i + 5 * sizeof(u32));
		data1 =
		    iwl_legacy_read_targ_mem(priv, base + i + 6 * sizeof(u32));

		IWL_ERR(priv,
			"%-13s (0x%X) %010u 0x%05X 0x%05X 0x%05X 0x%05X %u\n\n",
			iwl3945_desc_lookup(desc), desc, time, blink1, blink2,
			ilink1, ilink2, data1);
		trace_iwlwifi_legacy_dev_ucode_error(priv, desc, time, data1, 0,
					0, blink1, blink2, ilink1, ilink2);
	}
}

#define EVENT_START_OFFSET  (6 * sizeof(u32))

/**
 * iwl3945_print_event_log - Dump error event log to syslog
 *
 */
static int iwl3945_print_event_log(struct iwl_priv *priv, u32 start_idx,
				  u32 num_events, u32 mode,
				  int pos, char **buf, size_t bufsz)
{
	u32 i;
	u32 base;       /* SRAM byte address of event log header */
	u32 event_size;	/* 2 u32s, or 3 u32s if timestamp recorded */
	u32 ptr;        /* SRAM byte address of log data */
	u32 ev, time, data; /* event log data */
	unsigned long reg_flags;

	if (num_events == 0)
		return pos;

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
	_iwl_legacy_write_direct32(priv, HBUS_TARG_MEM_RADDR, ptr);
	rmb();

	/* "time" is actually "data" for mode 0 (no timestamp).
	 * place event id # at far right for easier visual parsing. */
	for (i = 0; i < num_events; i++) {
		ev = _iwl_legacy_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		time = _iwl_legacy_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (mode == 0) {
			/* data, ev */
			if (bufsz) {
				pos += scnprintf(*buf + pos, bufsz - pos,
						"0x%08x:%04u\n",
						time, ev);
			} else {
				IWL_ERR(priv, "0x%08x\t%04u\n", time, ev);
				trace_iwlwifi_legacy_dev_ucode_event(priv, 0,
							      time, ev);
			}
		} else {
			data = _iwl_legacy_read_direct32(priv,
							HBUS_TARG_MEM_RDAT);
			if (bufsz) {
				pos += scnprintf(*buf + pos, bufsz - pos,
						"%010u:0x%08x:%04u\n",
						 time, data, ev);
			} else {
				IWL_ERR(priv, "%010u\t0x%08x\t%04u\n",
					time, data, ev);
				trace_iwlwifi_legacy_dev_ucode_event(priv, time,
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
 * iwl3945_print_last_event_logs - Dump the newest # of event log to syslog
 */
static int iwl3945_print_last_event_logs(struct iwl_priv *priv, u32 capacity,
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
			pos = iwl3945_print_event_log(priv,
					     capacity - (size - next_entry),
					     size - next_entry, mode,
					     pos, buf, bufsz);
			pos = iwl3945_print_event_log(priv, 0,
						      next_entry, mode,
						      pos, buf, bufsz);
		} else
			pos = iwl3945_print_event_log(priv, next_entry - size,
						      size, mode,
						      pos, buf, bufsz);
	} else {
		if (next_entry < size)
			pos = iwl3945_print_event_log(priv, 0,
						      next_entry, mode,
						      pos, buf, bufsz);
		else
			pos = iwl3945_print_event_log(priv, next_entry - size,
						      size, mode,
						      pos, buf, bufsz);
	}
	return pos;
}

#define DEFAULT_IWL3945_DUMP_EVENT_LOG_ENTRIES (20)

int iwl3945_dump_nic_event_log(struct iwl_priv *priv, bool full_log,
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

	base = le32_to_cpu(priv->card_alive.log_event_table_ptr);
	if (!iwl3945_hw_valid_rtc_data_addr(base)) {
		IWL_ERR(priv, "Invalid event log pointer 0x%08X\n", base);
		return  -EINVAL;
	}

	/* event log header */
	capacity = iwl_legacy_read_targ_mem(priv, base);
	mode = iwl_legacy_read_targ_mem(priv, base + (1 * sizeof(u32)));
	num_wraps = iwl_legacy_read_targ_mem(priv, base + (2 * sizeof(u32)));
	next_entry = iwl_legacy_read_targ_mem(priv, base + (3 * sizeof(u32)));

	if (capacity > priv->cfg->base_params->max_event_log_size) {
		IWL_ERR(priv, "Log capacity %d is bogus, limit to %d entries\n",
			capacity, priv->cfg->base_params->max_event_log_size);
		capacity = priv->cfg->base_params->max_event_log_size;
	}

	if (next_entry > priv->cfg->base_params->max_event_log_size) {
		IWL_ERR(priv, "Log write index %d is bogus, limit to %d\n",
			next_entry, priv->cfg->base_params->max_event_log_size);
		next_entry = priv->cfg->base_params->max_event_log_size;
	}

	size = num_wraps ? capacity : next_entry;

	/* bail out if nothing in log */
	if (size == 0) {
		IWL_ERR(priv, "Start IWL Event Log Dump: nothing in log\n");
		return pos;
	}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	if (!(iwl_legacy_get_debug_level(priv) & IWL_DL_FW_ERRORS) && !full_log)
		size = (size > DEFAULT_IWL3945_DUMP_EVENT_LOG_ENTRIES)
			? DEFAULT_IWL3945_DUMP_EVENT_LOG_ENTRIES : size;
#else
	size = (size > DEFAULT_IWL3945_DUMP_EVENT_LOG_ENTRIES)
		? DEFAULT_IWL3945_DUMP_EVENT_LOG_ENTRIES : size;
#endif

	IWL_ERR(priv, "Start IWL Event Log Dump: display last %d count\n",
		  size);

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	if (display) {
		if (full_log)
			bufsz = capacity * 48;
		else
			bufsz = size * 48;
		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;
	}
	if ((iwl_legacy_get_debug_level(priv) & IWL_DL_FW_ERRORS) || full_log) {
		/* if uCode has wrapped back to top of log,
		 * start at the oldest entry,
		 * i.e the next one that uCode would fill.
		 */
		if (num_wraps)
			pos = iwl3945_print_event_log(priv, next_entry,
						capacity - next_entry, mode,
						pos, buf, bufsz);

		/* (then/else) start at top of log */
		pos = iwl3945_print_event_log(priv, 0, next_entry, mode,
					      pos, buf, bufsz);
	} else
		pos = iwl3945_print_last_event_logs(priv, capacity, num_wraps,
						    next_entry, size, mode,
						    pos, buf, bufsz);
#else
	pos = iwl3945_print_last_event_logs(priv, capacity, num_wraps,
					    next_entry, size, mode,
					    pos, buf, bufsz);
#endif
	return pos;
}

static void iwl3945_irq_tasklet(struct iwl_priv *priv)
{
	u32 inta, handled = 0;
	u32 inta_fh;
	unsigned long flags;
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
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

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	if (iwl_legacy_get_debug_level(priv) & IWL_DL_ISR) {
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
	if (inta_fh & CSR39_FH_INT_RX_MASK)
		inta |= CSR_INT_BIT_FH_RX;
	if (inta_fh & CSR39_FH_INT_TX_MASK)
		inta |= CSR_INT_BIT_FH_TX;

	/* Now service all interrupt bits discovered above. */
	if (inta & CSR_INT_BIT_HW_ERR) {
		IWL_ERR(priv, "Hardware error detected.  Restarting.\n");

		/* Tell the device to stop sending interrupts */
		iwl_legacy_disable_interrupts(priv);

		priv->isr_stats.hw++;
		iwl_legacy_irq_handle_error(priv);

		handled |= CSR_INT_BIT_HW_ERR;

		return;
	}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	if (iwl_legacy_get_debug_level(priv) & (IWL_DL_ISR)) {
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

	/* Error detected by uCode */
	if (inta & CSR_INT_BIT_SW_ERR) {
		IWL_ERR(priv, "Microcode SW error detected. "
			"Restarting 0x%X.\n", inta);
		priv->isr_stats.sw++;
		iwl_legacy_irq_handle_error(priv);
		handled |= CSR_INT_BIT_SW_ERR;
	}

	/* uCode wakes up after power-down sleep */
	if (inta & CSR_INT_BIT_WAKEUP) {
		IWL_DEBUG_ISR(priv, "Wakeup interrupt\n");
		iwl_legacy_rx_queue_update_write_ptr(priv, &priv->rxq);
		iwl_legacy_txq_update_write_ptr(priv, &priv->txq[0]);
		iwl_legacy_txq_update_write_ptr(priv, &priv->txq[1]);
		iwl_legacy_txq_update_write_ptr(priv, &priv->txq[2]);
		iwl_legacy_txq_update_write_ptr(priv, &priv->txq[3]);
		iwl_legacy_txq_update_write_ptr(priv, &priv->txq[4]);
		iwl_legacy_txq_update_write_ptr(priv, &priv->txq[5]);

		priv->isr_stats.wakeup++;
		handled |= CSR_INT_BIT_WAKEUP;
	}

	/* All uCode command responses, including Tx command responses,
	 * Rx "responses" (frame-received notification), and other
	 * notifications from uCode come through here*/
	if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX)) {
		iwl3945_rx_handle(priv);
		priv->isr_stats.rx++;
		handled |= (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX);
	}

	if (inta & CSR_INT_BIT_FH_TX) {
		IWL_DEBUG_ISR(priv, "Tx interrupt\n");
		priv->isr_stats.tx++;

		iwl_write32(priv, CSR_FH_INT_STATUS, (1 << 6));
		iwl_legacy_write_direct32(priv, FH39_TCSR_CREDIT
					(FH39_SRVC_CHNL), 0x0);
		handled |= CSR_INT_BIT_FH_TX;
	}

	if (inta & ~handled) {
		IWL_ERR(priv, "Unhandled INTA bits 0x%08x\n", inta & ~handled);
		priv->isr_stats.unhandled++;
	}

	if (inta & ~priv->inta_mask) {
		IWL_WARN(priv, "Disabled INTA bits 0x%08x were pending\n",
			 inta & ~priv->inta_mask);
		IWL_WARN(priv, "   with FH_INT = 0x%08x\n", inta_fh);
	}

	/* Re-enable all interrupts */
	/* only Re-enable if disabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl_legacy_enable_interrupts(priv);

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	if (iwl_legacy_get_debug_level(priv) & (IWL_DL_ISR)) {
		inta = iwl_read32(priv, CSR_INT);
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);
		IWL_DEBUG_ISR(priv, "End inta 0x%08x, enabled 0x%08x, fh 0x%08x, "
			"flags 0x%08lx\n", inta, inta_mask, inta_fh, flags);
	}
#endif
}

static int iwl3945_get_single_channel_for_scan(struct iwl_priv *priv,
					       struct ieee80211_vif *vif,
					       enum ieee80211_band band,
					       struct iwl3945_scan_channel *scan_ch)
{
	const struct ieee80211_supported_band *sband;
	u16 passive_dwell = 0;
	u16 active_dwell = 0;
	int added = 0;
	u8 channel = 0;

	sband = iwl_get_hw_mode(priv, band);
	if (!sband) {
		IWL_ERR(priv, "invalid band\n");
		return added;
	}

	active_dwell = iwl_legacy_get_active_dwell_time(priv, band, 0);
	passive_dwell = iwl_legacy_get_passive_dwell_time(priv, band, vif);

	if (passive_dwell <= active_dwell)
		passive_dwell = active_dwell + 1;


	channel = iwl_legacy_get_single_channel_number(priv, band);

	if (channel) {
		scan_ch->channel = channel;
		scan_ch->type = 0;	/* passive */
		scan_ch->active_dwell = cpu_to_le16(active_dwell);
		scan_ch->passive_dwell = cpu_to_le16(passive_dwell);
		/* Set txpower levels to defaults */
		scan_ch->tpc.dsp_atten = 110;
		if (band == IEEE80211_BAND_5GHZ)
			scan_ch->tpc.tx_gain = ((1 << 5) | (3 << 3)) | 3;
		else
			scan_ch->tpc.tx_gain = ((1 << 5) | (5 << 3));
		added++;
	} else
		IWL_ERR(priv, "no valid channel found\n");
	return added;
}

static int iwl3945_get_channels_for_scan(struct iwl_priv *priv,
					 enum ieee80211_band band,
				     u8 is_active, u8 n_probes,
				     struct iwl3945_scan_channel *scan_ch,
				     struct ieee80211_vif *vif)
{
	struct ieee80211_channel *chan;
	const struct ieee80211_supported_band *sband;
	const struct iwl_channel_info *ch_info;
	u16 passive_dwell = 0;
	u16 active_dwell = 0;
	int added, i;

	sband = iwl_get_hw_mode(priv, band);
	if (!sband)
		return 0;

	active_dwell = iwl_legacy_get_active_dwell_time(priv, band, n_probes);
	passive_dwell = iwl_legacy_get_passive_dwell_time(priv, band, vif);

	if (passive_dwell <= active_dwell)
		passive_dwell = active_dwell + 1;

	for (i = 0, added = 0; i < priv->scan_request->n_channels; i++) {
		chan = priv->scan_request->channels[i];

		if (chan->band != band)
			continue;

		scan_ch->channel = chan->hw_value;

		ch_info = iwl_legacy_get_channel_info(priv, band,
							scan_ch->channel);
		if (!iwl_legacy_is_channel_valid(ch_info)) {
			IWL_DEBUG_SCAN(priv,
				"Channel %d is INVALID for this band.\n",
			       scan_ch->channel);
			continue;
		}

		scan_ch->active_dwell = cpu_to_le16(active_dwell);
		scan_ch->passive_dwell = cpu_to_le16(passive_dwell);
		/* If passive , set up for auto-switch
		 *  and use long active_dwell time.
		 */
		if (!is_active || iwl_legacy_is_channel_passive(ch_info) ||
		    (chan->flags & IEEE80211_CHAN_PASSIVE_SCAN)) {
			scan_ch->type = 0;	/* passive */
			if (IWL_UCODE_API(priv->ucode_ver) == 1)
				scan_ch->active_dwell = cpu_to_le16(passive_dwell - 1);
		} else {
			scan_ch->type = 1;	/* active */
		}

		/* Set direct probe bits. These may be used both for active
		 * scan channels (probes gets sent right away),
		 * or for passive channels (probes get se sent only after
		 * hearing clear Rx packet).*/
		if (IWL_UCODE_API(priv->ucode_ver) >= 2) {
			if (n_probes)
				scan_ch->type |= IWL39_SCAN_PROBE_MASK(n_probes);
		} else {
			/* uCode v1 does not allow setting direct probe bits on
			 * passive channel. */
			if ((scan_ch->type & 1) && n_probes)
				scan_ch->type |= IWL39_SCAN_PROBE_MASK(n_probes);
		}

		/* Set txpower levels to defaults */
		scan_ch->tpc.dsp_atten = 110;
		/* scan_pwr_info->tpc.dsp_atten; */

		/*scan_pwr_info->tpc.tx_gain; */
		if (band == IEEE80211_BAND_5GHZ)
			scan_ch->tpc.tx_gain = ((1 << 5) | (3 << 3)) | 3;
		else {
			scan_ch->tpc.tx_gain = ((1 << 5) | (5 << 3));
			/* NOTE: if we were doing 6Mb OFDM for scans we'd use
			 * power level:
			 * scan_ch->tpc.tx_gain = ((1 << 5) | (2 << 3)) | 3;
			 */
		}

		IWL_DEBUG_SCAN(priv, "Scanning %d [%s %d]\n",
			       scan_ch->channel,
			       (scan_ch->type & 1) ? "ACTIVE" : "PASSIVE",
			       (scan_ch->type & 1) ?
			       active_dwell : passive_dwell);

		scan_ch++;
		added++;
	}

	IWL_DEBUG_SCAN(priv, "total channels to scan %d\n", added);
	return added;
}

static void iwl3945_init_hw_rates(struct iwl_priv *priv,
			      struct ieee80211_rate *rates)
{
	int i;

	for (i = 0; i < IWL_RATE_COUNT_LEGACY; i++) {
		rates[i].bitrate = iwl3945_rates[i].ieee * 5;
		rates[i].hw_value = i; /* Rate scaling will work on indexes */
		rates[i].hw_value_short = i;
		rates[i].flags = 0;
		if ((i > IWL39_LAST_OFDM_RATE) || (i < IWL_FIRST_OFDM_RATE)) {
			/*
			 * If CCK != 1M then set short preamble rate flag.
			 */
			rates[i].flags |= (iwl3945_rates[i].plcp == 10) ?
				0 : IEEE80211_RATE_SHORT_PREAMBLE;
		}
	}
}

/******************************************************************************
 *
 * uCode download functions
 *
 ******************************************************************************/

static void iwl3945_dealloc_ucode_pci(struct iwl_priv *priv)
{
	iwl_legacy_free_fw_desc(priv->pci_dev, &priv->ucode_code);
	iwl_legacy_free_fw_desc(priv->pci_dev, &priv->ucode_data);
	iwl_legacy_free_fw_desc(priv->pci_dev, &priv->ucode_data_backup);
	iwl_legacy_free_fw_desc(priv->pci_dev, &priv->ucode_init);
	iwl_legacy_free_fw_desc(priv->pci_dev, &priv->ucode_init_data);
	iwl_legacy_free_fw_desc(priv->pci_dev, &priv->ucode_boot);
}

/**
 * iwl3945_verify_inst_full - verify runtime uCode image in card vs. host,
 *     looking at all data.
 */
static int iwl3945_verify_inst_full(struct iwl_priv *priv, __le32 *image, u32 len)
{
	u32 val;
	u32 save_len = len;
	int rc = 0;
	u32 errcnt;

	IWL_DEBUG_INFO(priv, "ucode inst image size is %u\n", len);

	iwl_legacy_write_direct32(priv, HBUS_TARG_MEM_RADDR,
			       IWL39_RTC_INST_LOWER_BOUND);

	errcnt = 0;
	for (; len > 0; len -= sizeof(u32), image++) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		val = _iwl_legacy_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			IWL_ERR(priv, "uCode INST section is invalid at "
				  "offset 0x%x, is 0x%x, s/b 0x%x\n",
				  save_len - len, val, le32_to_cpu(*image));
			rc = -EIO;
			errcnt++;
			if (errcnt >= 20)
				break;
		}
	}


	if (!errcnt)
		IWL_DEBUG_INFO(priv,
			"ucode image in INSTRUCTION memory is good\n");

	return rc;
}


/**
 * iwl3945_verify_inst_sparse - verify runtime uCode image in card vs. host,
 *   using sample data 100 bytes apart.  If these sample points are good,
 *   it's a pretty good bet that everything between them is good, too.
 */
static int iwl3945_verify_inst_sparse(struct iwl_priv *priv, __le32 *image, u32 len)
{
	u32 val;
	int rc = 0;
	u32 errcnt = 0;
	u32 i;

	IWL_DEBUG_INFO(priv, "ucode inst image size is %u\n", len);

	for (i = 0; i < len; i += 100, image += 100/sizeof(u32)) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		iwl_legacy_write_direct32(priv, HBUS_TARG_MEM_RADDR,
			i + IWL39_RTC_INST_LOWER_BOUND);
		val = _iwl_legacy_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
#if 0 /* Enable this if you want to see details */
			IWL_ERR(priv, "uCode INST section is invalid at "
				  "offset 0x%x, is 0x%x, s/b 0x%x\n",
				  i, val, *image);
#endif
			rc = -EIO;
			errcnt++;
			if (errcnt >= 3)
				break;
		}
	}

	return rc;
}


/**
 * iwl3945_verify_ucode - determine which instruction image is in SRAM,
 *    and verify its contents
 */
static int iwl3945_verify_ucode(struct iwl_priv *priv)
{
	__le32 *image;
	u32 len;
	int rc = 0;

	/* Try bootstrap */
	image = (__le32 *)priv->ucode_boot.v_addr;
	len = priv->ucode_boot.len;
	rc = iwl3945_verify_inst_sparse(priv, image, len);
	if (rc == 0) {
		IWL_DEBUG_INFO(priv, "Bootstrap uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try initialize */
	image = (__le32 *)priv->ucode_init.v_addr;
	len = priv->ucode_init.len;
	rc = iwl3945_verify_inst_sparse(priv, image, len);
	if (rc == 0) {
		IWL_DEBUG_INFO(priv, "Initialize uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try runtime/protocol */
	image = (__le32 *)priv->ucode_code.v_addr;
	len = priv->ucode_code.len;
	rc = iwl3945_verify_inst_sparse(priv, image, len);
	if (rc == 0) {
		IWL_DEBUG_INFO(priv, "Runtime uCode is good in inst SRAM\n");
		return 0;
	}

	IWL_ERR(priv, "NO VALID UCODE IMAGE IN INSTRUCTION SRAM!!\n");

	/* Since nothing seems to match, show first several data entries in
	 * instruction SRAM, so maybe visual inspection will give a clue.
	 * Selection of bootstrap image (vs. other images) is arbitrary. */
	image = (__le32 *)priv->ucode_boot.v_addr;
	len = priv->ucode_boot.len;
	rc = iwl3945_verify_inst_full(priv, image, len);

	return rc;
}

static void iwl3945_nic_start(struct iwl_priv *priv)
{
	/* Remove all resets to allow NIC to operate */
	iwl_write32(priv, CSR_RESET, 0);
}

#define IWL3945_UCODE_GET(item)						\
static u32 iwl3945_ucode_get_##item(const struct iwl_ucode_header *ucode)\
{									\
	return le32_to_cpu(ucode->v1.item);				\
}

static u32 iwl3945_ucode_get_header_size(u32 api_ver)
{
	return 24;
}

static u8 *iwl3945_ucode_get_data(const struct iwl_ucode_header *ucode)
{
	return (u8 *) ucode->v1.data;
}

IWL3945_UCODE_GET(inst_size);
IWL3945_UCODE_GET(data_size);
IWL3945_UCODE_GET(init_size);
IWL3945_UCODE_GET(init_data_size);
IWL3945_UCODE_GET(boot_size);

/**
 * iwl3945_read_ucode - Read uCode images from disk file.
 *
 * Copy into buffers for card to fetch via bus-mastering
 */
static int iwl3945_read_ucode(struct iwl_priv *priv)
{
	const struct iwl_ucode_header *ucode;
	int ret = -EINVAL, index;
	const struct firmware *ucode_raw;
	/* firmware file name contains uCode/driver compatibility version */
	const char *name_pre = priv->cfg->fw_name_pre;
	const unsigned int api_max = priv->cfg->ucode_api_max;
	const unsigned int api_min = priv->cfg->ucode_api_min;
	char buf[25];
	u8 *src;
	size_t len;
	u32 api_ver, inst_size, data_size, init_size, init_data_size, boot_size;

	/* Ask kernel firmware_class module to get the boot firmware off disk.
	 * request_firmware() is synchronous, file is in memory on return. */
	for (index = api_max; index >= api_min; index--) {
		sprintf(buf, "%s%u%s", name_pre, index, ".ucode");
		ret = request_firmware(&ucode_raw, buf, &priv->pci_dev->dev);
		if (ret < 0) {
			IWL_ERR(priv, "%s firmware file req failed: %d\n",
				  buf, ret);
			if (ret == -ENOENT)
				continue;
			else
				goto error;
		} else {
			if (index < api_max)
				IWL_ERR(priv, "Loaded firmware %s, "
					"which is deprecated. "
					" Please use API v%u instead.\n",
					  buf, api_max);
			IWL_DEBUG_INFO(priv, "Got firmware '%s' file "
				       "(%zd bytes) from disk\n",
				       buf, ucode_raw->size);
			break;
		}
	}

	if (ret < 0)
		goto error;

	/* Make sure that we got at least our header! */
	if (ucode_raw->size <  iwl3945_ucode_get_header_size(1)) {
		IWL_ERR(priv, "File size way too small!\n");
		ret = -EINVAL;
		goto err_release;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (struct iwl_ucode_header *)ucode_raw->data;

	priv->ucode_ver = le32_to_cpu(ucode->ver);
	api_ver = IWL_UCODE_API(priv->ucode_ver);
	inst_size = iwl3945_ucode_get_inst_size(ucode);
	data_size = iwl3945_ucode_get_data_size(ucode);
	init_size = iwl3945_ucode_get_init_size(ucode);
	init_data_size = iwl3945_ucode_get_init_data_size(ucode);
	boot_size = iwl3945_ucode_get_boot_size(ucode);
	src = iwl3945_ucode_get_data(ucode);

	/* api_ver should match the api version forming part of the
	 * firmware filename ... but we don't check for that and only rely
	 * on the API version read from firmware header from here on forward */

	if (api_ver < api_min || api_ver > api_max) {
		IWL_ERR(priv, "Driver unable to support your firmware API. "
			  "Driver supports v%u, firmware is v%u.\n",
			  api_max, api_ver);
		priv->ucode_ver = 0;
		ret = -EINVAL;
		goto err_release;
	}
	if (api_ver != api_max)
		IWL_ERR(priv, "Firmware has old API version. Expected %u, "
			  "got %u. New firmware can be obtained "
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


	/* Verify size of file vs. image size info in file's header */
	if (ucode_raw->size != iwl3945_ucode_get_header_size(api_ver) +
		inst_size + data_size + init_size +
		init_data_size + boot_size) {

		IWL_DEBUG_INFO(priv,
			"uCode file size %zd does not match expected size\n",
			ucode_raw->size);
		ret = -EINVAL;
		goto err_release;
	}

	/* Verify that uCode images will fit in card's SRAM */
	if (inst_size > IWL39_MAX_INST_SIZE) {
		IWL_DEBUG_INFO(priv, "uCode instr len %d too large to fit in\n",
			       inst_size);
		ret = -EINVAL;
		goto err_release;
	}

	if (data_size > IWL39_MAX_DATA_SIZE) {
		IWL_DEBUG_INFO(priv, "uCode data len %d too large to fit in\n",
			       data_size);
		ret = -EINVAL;
		goto err_release;
	}
	if (init_size > IWL39_MAX_INST_SIZE) {
		IWL_DEBUG_INFO(priv,
				"uCode init instr len %d too large to fit in\n",
				init_size);
		ret = -EINVAL;
		goto err_release;
	}
	if (init_data_size > IWL39_MAX_DATA_SIZE) {
		IWL_DEBUG_INFO(priv,
				"uCode init data len %d too large to fit in\n",
				init_data_size);
		ret = -EINVAL;
		goto err_release;
	}
	if (boot_size > IWL39_MAX_BSM_SIZE) {
		IWL_DEBUG_INFO(priv,
				"uCode boot instr len %d too large to fit in\n",
				boot_size);
		ret = -EINVAL;
		goto err_release;
	}

	/* Allocate ucode buffers for card's bus-master loading ... */

	/* Runtime instructions and 2 copies of data:
	 * 1) unmodified from disk
	 * 2) backup cache for save/restore during power-downs */
	priv->ucode_code.len = inst_size;
	iwl_legacy_alloc_fw_desc(priv->pci_dev, &priv->ucode_code);

	priv->ucode_data.len = data_size;
	iwl_legacy_alloc_fw_desc(priv->pci_dev, &priv->ucode_data);

	priv->ucode_data_backup.len = data_size;
	iwl_legacy_alloc_fw_desc(priv->pci_dev, &priv->ucode_data_backup);

	if (!priv->ucode_code.v_addr || !priv->ucode_data.v_addr ||
	    !priv->ucode_data_backup.v_addr)
		goto err_pci_alloc;

	/* Initialization instructions and data */
	if (init_size && init_data_size) {
		priv->ucode_init.len = init_size;
		iwl_legacy_alloc_fw_desc(priv->pci_dev, &priv->ucode_init);

		priv->ucode_init_data.len = init_data_size;
		iwl_legacy_alloc_fw_desc(priv->pci_dev, &priv->ucode_init_data);

		if (!priv->ucode_init.v_addr || !priv->ucode_init_data.v_addr)
			goto err_pci_alloc;
	}

	/* Bootstrap (instructions only, no data) */
	if (boot_size) {
		priv->ucode_boot.len = boot_size;
		iwl_legacy_alloc_fw_desc(priv->pci_dev, &priv->ucode_boot);

		if (!priv->ucode_boot.v_addr)
			goto err_pci_alloc;
	}

	/* Copy images into buffers for card's bus-master reads ... */

	/* Runtime instructions (first block of data in file) */
	len = inst_size;
	IWL_DEBUG_INFO(priv,
		"Copying (but not loading) uCode instr len %zd\n", len);
	memcpy(priv->ucode_code.v_addr, src, len);
	src += len;

	IWL_DEBUG_INFO(priv, "uCode instr buf vaddr = 0x%p, paddr = 0x%08x\n",
		priv->ucode_code.v_addr, (u32)priv->ucode_code.p_addr);

	/* Runtime data (2nd block)
	 * NOTE:  Copy into backup buffer will be done in iwl3945_up()  */
	len = data_size;
	IWL_DEBUG_INFO(priv,
		"Copying (but not loading) uCode data len %zd\n", len);
	memcpy(priv->ucode_data.v_addr, src, len);
	memcpy(priv->ucode_data_backup.v_addr, src, len);
	src += len;

	/* Initialization instructions (3rd block) */
	if (init_size) {
		len = init_size;
		IWL_DEBUG_INFO(priv,
			"Copying (but not loading) init instr len %zd\n", len);
		memcpy(priv->ucode_init.v_addr, src, len);
		src += len;
	}

	/* Initialization data (4th block) */
	if (init_data_size) {
		len = init_data_size;
		IWL_DEBUG_INFO(priv,
			"Copying (but not loading) init data len %zd\n", len);
		memcpy(priv->ucode_init_data.v_addr, src, len);
		src += len;
	}

	/* Bootstrap instructions (5th block) */
	len = boot_size;
	IWL_DEBUG_INFO(priv,
		"Copying (but not loading) boot instr len %zd\n", len);
	memcpy(priv->ucode_boot.v_addr, src, len);

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);
	return 0;

 err_pci_alloc:
	IWL_ERR(priv, "failed to allocate pci memory\n");
	ret = -ENOMEM;
	iwl3945_dealloc_ucode_pci(priv);

 err_release:
	release_firmware(ucode_raw);

 error:
	return ret;
}


/**
 * iwl3945_set_ucode_ptrs - Set uCode address location
 *
 * Tell initialization uCode where to find runtime uCode.
 *
 * BSM registers initially contain pointers to initialization uCode.
 * We need to replace them to load runtime uCode inst and data,
 * and to save runtime data when powering down.
 */
static int iwl3945_set_ucode_ptrs(struct iwl_priv *priv)
{
	dma_addr_t pinst;
	dma_addr_t pdata;

	/* bits 31:0 for 3945 */
	pinst = priv->ucode_code.p_addr;
	pdata = priv->ucode_data_backup.p_addr;

	/* Tell bootstrap uCode where to find image to load */
	iwl_legacy_write_prph(priv, BSM_DRAM_INST_PTR_REG, pinst);
	iwl_legacy_write_prph(priv, BSM_DRAM_DATA_PTR_REG, pdata);
	iwl_legacy_write_prph(priv, BSM_DRAM_DATA_BYTECOUNT_REG,
				 priv->ucode_data.len);

	/* Inst byte count must be last to set up, bit 31 signals uCode
	 *   that all new ptr/size info is in place */
	iwl_legacy_write_prph(priv, BSM_DRAM_INST_BYTECOUNT_REG,
				 priv->ucode_code.len | BSM_DRAM_INST_LOAD);

	IWL_DEBUG_INFO(priv, "Runtime uCode pointers are set.\n");

	return 0;
}

/**
 * iwl3945_init_alive_start - Called after REPLY_ALIVE notification received
 *
 * Called after REPLY_ALIVE notification received from "initialize" uCode.
 *
 * Tell "initialize" uCode to go ahead and load the runtime uCode.
 */
static void iwl3945_init_alive_start(struct iwl_priv *priv)
{
	/* Check alive response for "valid" sign from uCode */
	if (priv->card_alive_init.is_valid != UCODE_VALID_OK) {
		/* We had an error bringing up the hardware, so take it
		 * all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Initialize Alive failed.\n");
		goto restart;
	}

	/* Bootstrap uCode has loaded initialize uCode ... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "initialize" alive if code weren't properly loaded.  */
	if (iwl3945_verify_ucode(priv)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Bad \"initialize\" uCode load.\n");
		goto restart;
	}

	/* Send pointers to protocol/runtime uCode image ... init code will
	 * load and launch runtime uCode, which will send us another "Alive"
	 * notification. */
	IWL_DEBUG_INFO(priv, "Initialization Alive received.\n");
	if (iwl3945_set_ucode_ptrs(priv)) {
		/* Runtime instruction load won't happen;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Couldn't set up uCode pointers.\n");
		goto restart;
	}
	return;

 restart:
	queue_work(priv->workqueue, &priv->restart);
}

/**
 * iwl3945_alive_start - called after REPLY_ALIVE notification received
 *                   from protocol/runtime uCode (initialization uCode's
 *                   Alive gets handled by iwl3945_init_alive_start()).
 */
static void iwl3945_alive_start(struct iwl_priv *priv)
{
	int thermal_spin = 0;
	u32 rfkill;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

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
	if (iwl3945_verify_ucode(priv)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Bad runtime uCode load.\n");
		goto restart;
	}

	rfkill = iwl_legacy_read_prph(priv, APMG_RFKILL_REG);
	IWL_DEBUG_INFO(priv, "RFKILL status: 0x%x\n", rfkill);

	if (rfkill & 0x1) {
		clear_bit(STATUS_RF_KILL_HW, &priv->status);
		/* if RFKILL is not on, then wait for thermal
		 * sensor in adapter to kick in */
		while (iwl3945_hw_get_temperature(priv) == 0) {
			thermal_spin++;
			udelay(10);
		}

		if (thermal_spin)
			IWL_DEBUG_INFO(priv, "Thermal calibration took %dus\n",
				       thermal_spin * 10);
	} else
		set_bit(STATUS_RF_KILL_HW, &priv->status);

	/* After the ALIVE response, we can send commands to 3945 uCode */
	set_bit(STATUS_ALIVE, &priv->status);

	/* Enable watchdog to monitor the driver tx queues */
	iwl_legacy_setup_watchdog(priv);

	if (iwl_legacy_is_rfkill(priv))
		return;

	ieee80211_wake_queues(priv->hw);

	priv->active_rate = IWL_RATES_MASK_3945;

	iwl_legacy_power_update_mode(priv, true);

	if (iwl_legacy_is_associated(priv, IWL_RXON_CTX_BSS)) {
		struct iwl3945_rxon_cmd *active_rxon =
				(struct iwl3945_rxon_cmd *)(&ctx->active);

		ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	} else {
		/* Initialize our rx_config data */
		iwl_legacy_connection_init_rx_config(priv, ctx);
	}

	/* Configure Bluetooth device coexistence support */
	iwl_legacy_send_bt_config(priv);

	set_bit(STATUS_READY, &priv->status);

	/* Configure the adapter for unassociated operation */
	iwl3945_commit_rxon(priv, ctx);

	iwl3945_reg_txpower_periodic(priv);

	IWL_DEBUG_INFO(priv, "ALIVE processing complete.\n");
	wake_up(&priv->wait_command_queue);

	return;

 restart:
	queue_work(priv->workqueue, &priv->restart);
}

static void iwl3945_cancel_deferred_work(struct iwl_priv *priv);

static void __iwl3945_down(struct iwl_priv *priv)
{
	unsigned long flags;
	int exit_pending;

	IWL_DEBUG_INFO(priv, DRV_NAME " is going down\n");

	iwl_legacy_scan_cancel_timeout(priv, 200);

	exit_pending = test_and_set_bit(STATUS_EXIT_PENDING, &priv->status);

	/* Stop TX queues watchdog. We need to have STATUS_EXIT_PENDING bit set
	 * to prevent rearm timer */
	del_timer_sync(&priv->watchdog);

	/* Station information will now be cleared in device */
	iwl_legacy_clear_ucode_stations(priv, NULL);
	iwl_legacy_dealloc_bcast_stations(priv);
	iwl_legacy_clear_driver_stations(priv);

	/* Unblock any waiting calls */
	wake_up_all(&priv->wait_command_queue);

	/* Wipe out the EXIT_PENDING status bit if we are not actually
	 * exiting the module */
	if (!exit_pending)
		clear_bit(STATUS_EXIT_PENDING, &priv->status);

	/* stop and reset the on-board processor */
	iwl_write32(priv, CSR_RESET, CSR_RESET_REG_FLAG_NEVO_RESET);

	/* tell the device to stop sending interrupts */
	spin_lock_irqsave(&priv->lock, flags);
	iwl_legacy_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
	iwl3945_synchronize_irq(priv);

	if (priv->mac80211_registered)
		ieee80211_stop_queues(priv->hw);

	/* If we have not previously called iwl3945_init() then
	 * clear all bits but the RF Kill bits and return */
	if (!iwl_legacy_is_init(priv)) {
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

	iwl3945_hw_txq_ctx_stop(priv);
	iwl3945_hw_rxq_stop(priv);

	/* Power-down device's busmaster DMA clocks */
	iwl_legacy_write_prph(priv, APMG_CLK_DIS_REG, APMG_CLK_VAL_DMA_CLK_RQT);
	udelay(5);

	/* Stop the device, and put it in low power state */
	iwl_legacy_apm_stop(priv);

 exit:
	memset(&priv->card_alive, 0, sizeof(struct iwl_alive_resp));

	if (priv->beacon_skb)
		dev_kfree_skb(priv->beacon_skb);
	priv->beacon_skb = NULL;

	/* clear out any free frames */
	iwl3945_clear_free_frames(priv);
}

static void iwl3945_down(struct iwl_priv *priv)
{
	mutex_lock(&priv->mutex);
	__iwl3945_down(priv);
	mutex_unlock(&priv->mutex);

	iwl3945_cancel_deferred_work(priv);
}

#define MAX_HW_RESTARTS 5

static int iwl3945_alloc_bcast_station(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	unsigned long flags;
	u8 sta_id;

	spin_lock_irqsave(&priv->sta_lock, flags);
	sta_id = iwl_legacy_prep_station(priv, ctx,
					iwlegacy_bcast_addr, false, NULL);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Unable to prepare broadcast station\n");
		spin_unlock_irqrestore(&priv->sta_lock, flags);

		return -EINVAL;
	}

	priv->stations[sta_id].used |= IWL_STA_DRIVER_ACTIVE;
	priv->stations[sta_id].used |= IWL_STA_BCAST;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return 0;
}

static int __iwl3945_up(struct iwl_priv *priv)
{
	int rc, i;

	rc = iwl3945_alloc_bcast_station(priv);
	if (rc)
		return rc;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_WARN(priv, "Exit pending; will not bring the NIC up\n");
		return -EIO;
	}

	if (!priv->ucode_data_backup.v_addr || !priv->ucode_data.v_addr) {
		IWL_ERR(priv, "ucode not available for device bring up\n");
		return -EIO;
	}

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (iwl_read32(priv, CSR_GP_CNTRL) &
				CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &priv->status);
	else {
		set_bit(STATUS_RF_KILL_HW, &priv->status);
		IWL_WARN(priv, "Radio disabled by HW RF Kill switch\n");
		return -ENODEV;
	}

	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);

	rc = iwl3945_hw_nic_init(priv);
	if (rc) {
		IWL_ERR(priv, "Unable to int nic\n");
		return rc;
	}

	/* make sure rfkill handshake bits are cleared */
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable host interrupts */
	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);
	iwl_legacy_enable_interrupts(priv);

	/* really make sure rfkill handshake bits are cleared */
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);

	/* Copy original ucode data image from disk into backup cache.
	 * This will be used to initialize the on-board processor's
	 * data SRAM for a clean start when the runtime program first loads. */
	memcpy(priv->ucode_data_backup.v_addr, priv->ucode_data.v_addr,
	       priv->ucode_data.len);

	/* We return success when we resume from suspend and rf_kill is on. */
	if (test_bit(STATUS_RF_KILL_HW, &priv->status))
		return 0;

	for (i = 0; i < MAX_HW_RESTARTS; i++) {

		/* load bootstrap state machine,
		 * load bootstrap program into processor's memory,
		 * prepare to load the "initialize" uCode */
		rc = priv->cfg->ops->lib->load_ucode(priv);

		if (rc) {
			IWL_ERR(priv,
				"Unable to set up bootstrap uCode: %d\n", rc);
			continue;
		}

		/* start card; "initialize" will load runtime ucode */
		iwl3945_nic_start(priv);

		IWL_DEBUG_INFO(priv, DRV_NAME " is coming up\n");

		return 0;
	}

	set_bit(STATUS_EXIT_PENDING, &priv->status);
	__iwl3945_down(priv);
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

static void iwl3945_bg_init_alive_start(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, init_alive_start.work);

	mutex_lock(&priv->mutex);
	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		goto out;

	iwl3945_init_alive_start(priv);
out:
	mutex_unlock(&priv->mutex);
}

static void iwl3945_bg_alive_start(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, alive_start.work);

	mutex_lock(&priv->mutex);
	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		goto out;

	iwl3945_alive_start(priv);
out:
	mutex_unlock(&priv->mutex);
}

/*
 * 3945 cannot interrupt driver when hardware rf kill switch toggles;
 * driver must poll CSR_GP_CNTRL_REG register for change.  This register
 * *is* readable even when device has been SW_RESET into low power mode
 * (e.g. during RF KILL).
 */
static void iwl3945_rfkill_poll(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, _3945.rfkill_poll.work);
	bool old_rfkill = test_bit(STATUS_RF_KILL_HW, &priv->status);
	bool new_rfkill = !(iwl_read32(priv, CSR_GP_CNTRL)
			& CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW);

	if (new_rfkill != old_rfkill) {
		if (new_rfkill)
			set_bit(STATUS_RF_KILL_HW, &priv->status);
		else
			clear_bit(STATUS_RF_KILL_HW, &priv->status);

		wiphy_rfkill_set_hw_state(priv->hw->wiphy, new_rfkill);

		IWL_DEBUG_RF_KILL(priv, "RF_KILL bit toggled to %s.\n",
				new_rfkill ? "disable radio" : "enable radio");
	}

	/* Keep this running, even if radio now enabled.  This will be
	 * cancelled in mac_start() if system decides to start again */
	queue_delayed_work(priv->workqueue, &priv->_3945.rfkill_poll,
			   round_jiffies_relative(2 * HZ));

}

int iwl3945_request_scan(struct iwl_priv *priv, struct ieee80211_vif *vif)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_SCAN_CMD,
		.len = sizeof(struct iwl3945_scan_cmd),
		.flags = CMD_SIZE_HUGE,
	};
	struct iwl3945_scan_cmd *scan;
	u8 n_probes = 0;
	enum ieee80211_band band;
	bool is_active = false;
	int ret;

	lockdep_assert_held(&priv->mutex);

	if (!priv->scan_cmd) {
		priv->scan_cmd = kmalloc(sizeof(struct iwl3945_scan_cmd) +
					 IWL_MAX_SCAN_SIZE, GFP_KERNEL);
		if (!priv->scan_cmd) {
			IWL_DEBUG_SCAN(priv, "Fail to allocate scan memory\n");
			return -ENOMEM;
		}
	}
	scan = priv->scan_cmd;
	memset(scan, 0, sizeof(struct iwl3945_scan_cmd) + IWL_MAX_SCAN_SIZE);

	scan->quiet_plcp_th = IWL_PLCP_QUIET_THRESH;
	scan->quiet_time = IWL_ACTIVE_QUIET_TIME;

	if (iwl_legacy_is_associated(priv, IWL_RXON_CTX_BSS)) {
		u16 interval = 0;
		u32 extra;
		u32 suspend_time = 100;
		u32 scan_suspend_time = 100;

		IWL_DEBUG_INFO(priv, "Scanning while associated...\n");

		if (priv->is_internal_short_scan)
			interval = 0;
		else
			interval = vif->bss_conf.beacon_int;

		scan->suspend_time = 0;
		scan->max_out_time = cpu_to_le32(200 * 1024);
		if (!interval)
			interval = suspend_time;
		/*
		 * suspend time format:
		 *  0-19: beacon interval in usec (time before exec.)
		 * 20-23: 0
		 * 24-31: number of beacons (suspend between channels)
		 */

		extra = (suspend_time / interval) << 24;
		scan_suspend_time = 0xFF0FFFFF &
		    (extra | ((suspend_time % interval) * 1024));

		scan->suspend_time = cpu_to_le32(scan_suspend_time);
		IWL_DEBUG_SCAN(priv, "suspend_time 0x%X beacon interval %d\n",
			       scan_suspend_time, interval);
	}

	if (priv->is_internal_short_scan) {
		IWL_DEBUG_SCAN(priv, "Start internal passive scan.\n");
	} else if (priv->scan_request->n_ssids) {
		int i, p = 0;
		IWL_DEBUG_SCAN(priv, "Kicking off active scan\n");
		for (i = 0; i < priv->scan_request->n_ssids; i++) {
			/* always does wildcard anyway */
			if (!priv->scan_request->ssids[i].ssid_len)
				continue;
			scan->direct_scan[p].id = WLAN_EID_SSID;
			scan->direct_scan[p].len =
				priv->scan_request->ssids[i].ssid_len;
			memcpy(scan->direct_scan[p].ssid,
			       priv->scan_request->ssids[i].ssid,
			       priv->scan_request->ssids[i].ssid_len);
			n_probes++;
			p++;
		}
		is_active = true;
	} else
		IWL_DEBUG_SCAN(priv, "Kicking off passive scan.\n");

	/* We don't build a direct scan probe request; the uCode will do
	 * that based on the direct_mask added to each channel entry */
	scan->tx_cmd.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK;
	scan->tx_cmd.sta_id = priv->contexts[IWL_RXON_CTX_BSS].bcast_sta_id;
	scan->tx_cmd.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;

	/* flags + rate selection */

	switch (priv->scan_band) {
	case IEEE80211_BAND_2GHZ:
		scan->flags = RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK;
		scan->tx_cmd.rate = IWL_RATE_1M_PLCP;
		band = IEEE80211_BAND_2GHZ;
		break;
	case IEEE80211_BAND_5GHZ:
		scan->tx_cmd.rate = IWL_RATE_6M_PLCP;
		band = IEEE80211_BAND_5GHZ;
		break;
	default:
		IWL_WARN(priv, "Invalid scan band\n");
		return -EIO;
	}
	/*
	 * If active scaning is requested but a certain channel is marked
	 * passive, we can do active scanning if we detect transmissions. For
	 * passive only scanning disable switching to active on any channel.
	 */
	scan->good_CRC_th = is_active ? IWL_GOOD_CRC_TH_DEFAULT :
					IWL_GOOD_CRC_TH_NEVER;

	if (!priv->is_internal_short_scan) {
		scan->tx_cmd.len = cpu_to_le16(
			iwl_legacy_fill_probe_req(priv,
				(struct ieee80211_mgmt *)scan->data,
				vif->addr,
				priv->scan_request->ie,
				priv->scan_request->ie_len,
				IWL_MAX_SCAN_SIZE - sizeof(*scan)));
	} else {
		/* use bcast addr, will not be transmitted but must be valid */
		scan->tx_cmd.len = cpu_to_le16(
			iwl_legacy_fill_probe_req(priv,
				(struct ieee80211_mgmt *)scan->data,
				iwlegacy_bcast_addr, NULL, 0,
				IWL_MAX_SCAN_SIZE - sizeof(*scan)));
	}
	/* select Rx antennas */
	scan->flags |= iwl3945_get_antenna_flags(priv);

	if (priv->is_internal_short_scan) {
		scan->channel_count =
			iwl3945_get_single_channel_for_scan(priv, vif, band,
				(void *)&scan->data[le16_to_cpu(
				scan->tx_cmd.len)]);
	} else {
		scan->channel_count =
			iwl3945_get_channels_for_scan(priv, band, is_active, n_probes,
				(void *)&scan->data[le16_to_cpu(scan->tx_cmd.len)], vif);
	}

	if (scan->channel_count == 0) {
		IWL_DEBUG_SCAN(priv, "channel count %d\n", scan->channel_count);
		return -EIO;
	}

	cmd.len += le16_to_cpu(scan->tx_cmd.len) +
	    scan->channel_count * sizeof(struct iwl3945_scan_channel);
	cmd.data = scan;
	scan->len = cpu_to_le16(cmd.len);

	set_bit(STATUS_SCAN_HW, &priv->status);
	ret = iwl_legacy_send_cmd_sync(priv, &cmd);
	if (ret)
		clear_bit(STATUS_SCAN_HW, &priv->status);
	return ret;
}

void iwl3945_post_scan(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	/*
	 * Since setting the RXON may have been deferred while
	 * performing the scan, fire one off if needed
	 */
	if (memcmp(&ctx->staging, &ctx->active, sizeof(ctx->staging)))
		iwl3945_commit_rxon(priv, ctx);
}

static void iwl3945_bg_restart(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, restart);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (test_and_clear_bit(STATUS_FW_ERROR, &priv->status)) {
		struct iwl_rxon_context *ctx;
		mutex_lock(&priv->mutex);
		for_each_context(priv, ctx)
			ctx->vif = NULL;
		priv->is_open = 0;
		mutex_unlock(&priv->mutex);
		iwl3945_down(priv);
		ieee80211_restart_hw(priv->hw);
	} else {
		iwl3945_down(priv);

		mutex_lock(&priv->mutex);
		if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
			mutex_unlock(&priv->mutex);
			return;
		}

		__iwl3945_up(priv);
		mutex_unlock(&priv->mutex);
	}
}

static void iwl3945_bg_rx_replenish(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, rx_replenish);

	mutex_lock(&priv->mutex);
	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		goto out;

	iwl3945_rx_replenish(priv);
out:
	mutex_unlock(&priv->mutex);
}

void iwl3945_post_associate(struct iwl_priv *priv)
{
	int rc = 0;
	struct ieee80211_conf *conf = NULL;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	if (!ctx->vif || !priv->is_open)
		return;

	IWL_DEBUG_ASSOC(priv, "Associated as %d to: %pM\n",
			ctx->vif->bss_conf.aid, ctx->active.bssid_addr);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	iwl_legacy_scan_cancel_timeout(priv, 200);

	conf = iwl_legacy_ieee80211_get_hw_conf(priv->hw);

	ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwl3945_commit_rxon(priv, ctx);

	rc = iwl_legacy_send_rxon_timing(priv, ctx);
	if (rc)
		IWL_WARN(priv, "REPLY_RXON_TIMING failed - "
			    "Attempting to continue.\n");

	ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;

	ctx->staging.assoc_id = cpu_to_le16(ctx->vif->bss_conf.aid);

	IWL_DEBUG_ASSOC(priv, "assoc id %d beacon interval %d\n",
			ctx->vif->bss_conf.aid, ctx->vif->bss_conf.beacon_int);

	if (ctx->vif->bss_conf.use_short_preamble)
		ctx->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		ctx->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;

	if (ctx->staging.flags & RXON_FLG_BAND_24G_MSK) {
		if (ctx->vif->bss_conf.use_short_slot)
			ctx->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			ctx->staging.flags &= ~RXON_FLG_SHORT_SLOT_MSK;
	}

	iwl3945_commit_rxon(priv, ctx);

	switch (ctx->vif->type) {
	case NL80211_IFTYPE_STATION:
		iwl3945_rate_scale_init(priv->hw, IWL_AP_ID);
		break;
	case NL80211_IFTYPE_ADHOC:
		iwl3945_send_beacon_cmd(priv);
		break;
	default:
		IWL_ERR(priv, "%s Should not be called in %d mode\n",
			__func__, ctx->vif->type);
		break;
	}
}

/*****************************************************************************
 *
 * mac80211 entry point functions
 *
 *****************************************************************************/

#define UCODE_READY_TIMEOUT	(2 * HZ)

static int iwl3945_mac_start(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	int ret;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	/* we should be verifying the device is ready to be opened */
	mutex_lock(&priv->mutex);

	/* fetch ucode file from disk, alloc and copy to bus-master buffers ...
	 * ucode filename and max sizes are card-specific. */

	if (!priv->ucode_code.len) {
		ret = iwl3945_read_ucode(priv);
		if (ret) {
			IWL_ERR(priv, "Could not read microcode: %d\n", ret);
			mutex_unlock(&priv->mutex);
			goto out_release_irq;
		}
	}

	ret = __iwl3945_up(priv);

	mutex_unlock(&priv->mutex);

	if (ret)
		goto out_release_irq;

	IWL_DEBUG_INFO(priv, "Start UP work.\n");

	/* Wait for START_ALIVE from ucode. Otherwise callbacks from
	 * mac80211 will not be run successfully. */
	ret = wait_event_timeout(priv->wait_command_queue,
			test_bit(STATUS_READY, &priv->status),
			UCODE_READY_TIMEOUT);
	if (!ret) {
		if (!test_bit(STATUS_READY, &priv->status)) {
			IWL_ERR(priv,
				"Wait for START_ALIVE timeout after %dms.\n",
				jiffies_to_msecs(UCODE_READY_TIMEOUT));
			ret = -ETIMEDOUT;
			goto out_release_irq;
		}
	}

	/* ucode is running and will send rfkill notifications,
	 * no need to poll the killswitch state anymore */
	cancel_delayed_work(&priv->_3945.rfkill_poll);

	priv->is_open = 1;
	IWL_DEBUG_MAC80211(priv, "leave\n");
	return 0;

out_release_irq:
	priv->is_open = 0;
	IWL_DEBUG_MAC80211(priv, "leave - failed\n");
	return ret;
}

static void iwl3945_mac_stop(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!priv->is_open) {
		IWL_DEBUG_MAC80211(priv, "leave - skip\n");
		return;
	}

	priv->is_open = 0;

	iwl3945_down(priv);

	flush_workqueue(priv->workqueue);

	/* start polling the killswitch state again */
	queue_delayed_work(priv->workqueue, &priv->_3945.rfkill_poll,
			   round_jiffies_relative(2 * HZ));

	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static void iwl3945_mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	IWL_DEBUG_TX(priv, "dev->xmit(%d bytes) at rate 0x%02x\n", skb->len,
		     ieee80211_get_tx_rate(hw, IEEE80211_SKB_CB(skb))->bitrate);

	if (iwl3945_tx_skb(priv, skb))
		dev_kfree_skb_any(skb);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}

void iwl3945_config_ap(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	struct ieee80211_vif *vif = ctx->vif;
	int rc = 0;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* The following should be done only at AP bring up */
	if (!(iwl_legacy_is_associated(priv, IWL_RXON_CTX_BSS))) {

		/* RXON - unassoc (to set timing command) */
		ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl3945_commit_rxon(priv, ctx);

		/* RXON Timing */
		rc = iwl_legacy_send_rxon_timing(priv, ctx);
		if (rc)
			IWL_WARN(priv, "REPLY_RXON_TIMING failed - "
					"Attempting to continue.\n");

		ctx->staging.assoc_id = 0;

		if (vif->bss_conf.use_short_preamble)
			ctx->staging.flags |=
				RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			ctx->staging.flags &=
				~RXON_FLG_SHORT_PREAMBLE_MSK;

		if (ctx->staging.flags & RXON_FLG_BAND_24G_MSK) {
			if (vif->bss_conf.use_short_slot)
				ctx->staging.flags |=
					RXON_FLG_SHORT_SLOT_MSK;
			else
				ctx->staging.flags &=
					~RXON_FLG_SHORT_SLOT_MSK;
		}
		/* restore RXON assoc */
		ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;
		iwl3945_commit_rxon(priv, ctx);
	}
	iwl3945_send_beacon_cmd(priv);
}

static int iwl3945_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct ieee80211_key_conf *key)
{
	struct iwl_priv *priv = hw->priv;
	int ret = 0;
	u8 sta_id = IWL_INVALID_STATION;
	u8 static_key;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (iwl3945_mod_params.sw_crypto) {
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

	static_key = !iwl_legacy_is_associated(priv, IWL_RXON_CTX_BSS);

	if (!static_key) {
		sta_id = iwl_legacy_sta_id_or_broadcast(
				priv, &priv->contexts[IWL_RXON_CTX_BSS], sta);
		if (sta_id == IWL_INVALID_STATION)
			return -EINVAL;
	}

	mutex_lock(&priv->mutex);
	iwl_legacy_scan_cancel_timeout(priv, 100);

	switch (cmd) {
	case SET_KEY:
		if (static_key)
			ret = iwl3945_set_static_key(priv, key);
		else
			ret = iwl3945_set_dynamic_key(priv, key, sta_id);
		IWL_DEBUG_MAC80211(priv, "enable hwcrypto key\n");
		break;
	case DISABLE_KEY:
		if (static_key)
			ret = iwl3945_remove_static_key(priv);
		else
			ret = iwl3945_clear_sta_key_info(priv, sta_id);
		IWL_DEBUG_MAC80211(priv, "disable hwcrypto key\n");
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&priv->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");

	return ret;
}

static int iwl3945_mac_sta_add(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl3945_sta_priv *sta_priv = (void *)sta->drv_priv;
	int ret;
	bool is_ap = vif->type == NL80211_IFTYPE_STATION;
	u8 sta_id;

	IWL_DEBUG_INFO(priv, "received request to add station %pM\n",
			sta->addr);
	mutex_lock(&priv->mutex);
	IWL_DEBUG_INFO(priv, "proceeding to add station %pM\n",
			sta->addr);
	sta_priv->common.sta_id = IWL_INVALID_STATION;


	ret = iwl_legacy_add_station_common(priv,
				&priv->contexts[IWL_RXON_CTX_BSS],
				     sta->addr, is_ap, sta, &sta_id);
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
	iwl3945_rs_rate_init(priv, sta, sta_id);
	mutex_unlock(&priv->mutex);

	return 0;
}

static void iwl3945_configure_filter(struct ieee80211_hw *hw,
				     unsigned int changed_flags,
				     unsigned int *total_flags,
				     u64 multicast)
{
	struct iwl_priv *priv = hw->priv;
	__le32 filter_or = 0, filter_nand = 0;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

#define CHK(test, flag)	do { \
	if (*total_flags & (test))		\
		filter_or |= (flag);		\
	else					\
		filter_nand |= (flag);		\
	} while (0)

	IWL_DEBUG_MAC80211(priv, "Enter: changed: 0x%x, total: 0x%x\n",
			changed_flags, *total_flags);

	CHK(FIF_OTHER_BSS | FIF_PROMISC_IN_BSS, RXON_FILTER_PROMISC_MSK);
	CHK(FIF_CONTROL, RXON_FILTER_CTL2HOST_MSK);
	CHK(FIF_BCN_PRBRESP_PROMISC, RXON_FILTER_BCON_AWARE_MSK);

#undef CHK

	mutex_lock(&priv->mutex);

	ctx->staging.filter_flags &= ~filter_nand;
	ctx->staging.filter_flags |= filter_or;

	/*
	 * Not committing directly because hardware can perform a scan,
	 * but even if hw is ready, committing here breaks for some reason,
	 * we'll eventually commit the filter flags change anyway.
	 */

	mutex_unlock(&priv->mutex);

	/*
	 * Receiving all multicast frames is always enabled by the
	 * default flags setup in iwl_legacy_connection_init_rx_config()
	 * since we currently do not support programming multicast
	 * filters into the device.
	 */
	*total_flags &= FIF_OTHER_BSS | FIF_ALLMULTI | FIF_PROMISC_IN_BSS |
			FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL;
}


/*****************************************************************************
 *
 * sysfs attributes
 *
 *****************************************************************************/

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG

/*
 * The following adds a new attribute to the sysfs representation
 * of this device driver (i.e. a new file in /sys/bus/pci/drivers/iwl/)
 * used for controlling the debug level.
 *
 * See the level definitions in iwl for details.
 *
 * The debug_level being managed using sysfs below is a per device debug
 * level that is used instead of the global debug level if it (the per
 * device debug level) is set.
 */
static ssize_t iwl3945_show_debug_level(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	return sprintf(buf, "0x%08X\n", iwl_legacy_get_debug_level(priv));
}
static ssize_t iwl3945_store_debug_level(struct device *d,
				struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	unsigned long val;
	int ret;

	ret = strict_strtoul(buf, 0, &val);
	if (ret)
		IWL_INFO(priv, "%s is not in hex or decimal form.\n", buf);
	else {
		priv->debug_level = val;
		if (iwl_legacy_alloc_traffic_mem(priv))
			IWL_ERR(priv,
				"Not enough memory to generate traffic log\n");
	}
	return strnlen(buf, count);
}

static DEVICE_ATTR(debug_level, S_IWUSR | S_IRUGO,
			iwl3945_show_debug_level, iwl3945_store_debug_level);

#endif /* CONFIG_IWLWIFI_LEGACY_DEBUG */

static ssize_t iwl3945_show_temperature(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	if (!iwl_legacy_is_alive(priv))
		return -EAGAIN;

	return sprintf(buf, "%d\n", iwl3945_hw_get_temperature(priv));
}

static DEVICE_ATTR(temperature, S_IRUGO, iwl3945_show_temperature, NULL);

static ssize_t iwl3945_show_tx_power(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	return sprintf(buf, "%d\n", priv->tx_power_user_lmt);
}

static ssize_t iwl3945_store_tx_power(struct device *d,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	char *p = (char *)buf;
	u32 val;

	val = simple_strtoul(p, &p, 10);
	if (p == buf)
		IWL_INFO(priv, ": %s is not in decimal form.\n", buf);
	else
		iwl3945_hw_reg_set_txpower(priv, val);

	return count;
}

static DEVICE_ATTR(tx_power, S_IWUSR | S_IRUGO, iwl3945_show_tx_power, iwl3945_store_tx_power);

static ssize_t iwl3945_show_flags(struct device *d,
			  struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	return sprintf(buf, "0x%04X\n", ctx->active.flags);
}

static ssize_t iwl3945_store_flags(struct device *d,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	u32 flags = simple_strtoul(buf, NULL, 0);
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	mutex_lock(&priv->mutex);
	if (le32_to_cpu(ctx->staging.flags) != flags) {
		/* Cancel any currently running scans... */
		if (iwl_legacy_scan_cancel_timeout(priv, 100))
			IWL_WARN(priv, "Could not cancel scan.\n");
		else {
			IWL_DEBUG_INFO(priv, "Committing rxon.flags = 0x%04X\n",
				       flags);
			ctx->staging.flags = cpu_to_le32(flags);
			iwl3945_commit_rxon(priv, ctx);
		}
	}
	mutex_unlock(&priv->mutex);

	return count;
}

static DEVICE_ATTR(flags, S_IWUSR | S_IRUGO, iwl3945_show_flags, iwl3945_store_flags);

static ssize_t iwl3945_show_filter_flags(struct device *d,
				 struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	return sprintf(buf, "0x%04X\n",
		le32_to_cpu(ctx->active.filter_flags));
}

static ssize_t iwl3945_store_filter_flags(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	u32 filter_flags = simple_strtoul(buf, NULL, 0);

	mutex_lock(&priv->mutex);
	if (le32_to_cpu(ctx->staging.filter_flags) != filter_flags) {
		/* Cancel any currently running scans... */
		if (iwl_legacy_scan_cancel_timeout(priv, 100))
			IWL_WARN(priv, "Could not cancel scan.\n");
		else {
			IWL_DEBUG_INFO(priv, "Committing rxon.filter_flags = "
				       "0x%04X\n", filter_flags);
			ctx->staging.filter_flags =
				cpu_to_le32(filter_flags);
			iwl3945_commit_rxon(priv, ctx);
		}
	}
	mutex_unlock(&priv->mutex);

	return count;
}

static DEVICE_ATTR(filter_flags, S_IWUSR | S_IRUGO, iwl3945_show_filter_flags,
		   iwl3945_store_filter_flags);

static ssize_t iwl3945_show_measurement(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	struct iwl_spectrum_notification measure_report;
	u32 size = sizeof(measure_report), len = 0, ofs = 0;
	u8 *data = (u8 *)&measure_report;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (!(priv->measurement_status & MEASUREMENT_READY)) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return 0;
	}
	memcpy(&measure_report, &priv->measure_report, size);
	priv->measurement_status = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

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

static ssize_t iwl3945_store_measurement(struct device *d,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	struct ieee80211_measurement_params params = {
		.channel = le16_to_cpu(ctx->active.channel),
		.start_time = cpu_to_le64(priv->_3945.last_tsf),
		.duration = cpu_to_le16(1),
	};
	u8 type = IWL_MEASURE_BASIC;
	u8 buffer[32];
	u8 channel;

	if (count) {
		char *p = buffer;
		strncpy(buffer, buf, min(sizeof(buffer), count));
		channel = simple_strtoul(p, NULL, 0);
		if (channel)
			params.channel = channel;

		p = buffer;
		while (*p && *p != ' ')
			p++;
		if (*p)
			type = simple_strtoul(p + 1, NULL, 0);
	}

	IWL_DEBUG_INFO(priv, "Invoking measurement of type %d on "
		       "channel %d (for '%s')\n", type, params.channel, buf);
	iwl3945_get_measurement(priv, &params, type);

	return count;
}

static DEVICE_ATTR(measurement, S_IRUSR | S_IWUSR,
		   iwl3945_show_measurement, iwl3945_store_measurement);

static ssize_t iwl3945_store_retry_rate(struct device *d,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	priv->retry_rate = simple_strtoul(buf, NULL, 0);
	if (priv->retry_rate <= 0)
		priv->retry_rate = 1;

	return count;
}

static ssize_t iwl3945_show_retry_rate(struct device *d,
			       struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	return sprintf(buf, "%d", priv->retry_rate);
}

static DEVICE_ATTR(retry_rate, S_IWUSR | S_IRUSR, iwl3945_show_retry_rate,
		   iwl3945_store_retry_rate);


static ssize_t iwl3945_show_channels(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	/* all this shit doesn't belong into sysfs anyway */
	return 0;
}

static DEVICE_ATTR(channels, S_IRUSR, iwl3945_show_channels, NULL);

static ssize_t iwl3945_show_antenna(struct device *d,
			    struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	if (!iwl_legacy_is_alive(priv))
		return -EAGAIN;

	return sprintf(buf, "%d\n", iwl3945_mod_params.antenna);
}

static ssize_t iwl3945_store_antenna(struct device *d,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct iwl_priv *priv __maybe_unused = dev_get_drvdata(d);
	int ant;

	if (count == 0)
		return 0;

	if (sscanf(buf, "%1i", &ant) != 1) {
		IWL_DEBUG_INFO(priv, "not in hex or decimal form.\n");
		return count;
	}

	if ((ant >= 0) && (ant <= 2)) {
		IWL_DEBUG_INFO(priv, "Setting antenna select to %d.\n", ant);
		iwl3945_mod_params.antenna = (enum iwl3945_antenna)ant;
	} else
		IWL_DEBUG_INFO(priv, "Bad antenna select value %d.\n", ant);


	return count;
}

static DEVICE_ATTR(antenna, S_IWUSR | S_IRUGO, iwl3945_show_antenna, iwl3945_store_antenna);

static ssize_t iwl3945_show_status(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	if (!iwl_legacy_is_alive(priv))
		return -EAGAIN;
	return sprintf(buf, "0x%08x\n", (int)priv->status);
}

static DEVICE_ATTR(status, S_IRUGO, iwl3945_show_status, NULL);

static ssize_t iwl3945_dump_error_log(struct device *d,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	char *p = (char *)buf;

	if (p[0] == '1')
		iwl3945_dump_nic_error_log(priv);

	return strnlen(buf, count);
}

static DEVICE_ATTR(dump_errors, S_IWUSR, NULL, iwl3945_dump_error_log);

/*****************************************************************************
 *
 * driver setup and tear down
 *
 *****************************************************************************/

static void iwl3945_setup_deferred_work(struct iwl_priv *priv)
{
	priv->workqueue = create_singlethread_workqueue(DRV_NAME);

	init_waitqueue_head(&priv->wait_command_queue);

	INIT_WORK(&priv->restart, iwl3945_bg_restart);
	INIT_WORK(&priv->rx_replenish, iwl3945_bg_rx_replenish);
	INIT_DELAYED_WORK(&priv->init_alive_start, iwl3945_bg_init_alive_start);
	INIT_DELAYED_WORK(&priv->alive_start, iwl3945_bg_alive_start);
	INIT_DELAYED_WORK(&priv->_3945.rfkill_poll, iwl3945_rfkill_poll);

	iwl_legacy_setup_scan_deferred_work(priv);

	iwl3945_hw_setup_deferred_work(priv);

	init_timer(&priv->watchdog);
	priv->watchdog.data = (unsigned long)priv;
	priv->watchdog.function = iwl_legacy_bg_watchdog;

	tasklet_init(&priv->irq_tasklet, (void (*)(unsigned long))
		     iwl3945_irq_tasklet, (unsigned long)priv);
}

static void iwl3945_cancel_deferred_work(struct iwl_priv *priv)
{
	iwl3945_hw_cancel_deferred_work(priv);

	cancel_delayed_work_sync(&priv->init_alive_start);
	cancel_delayed_work(&priv->alive_start);

	iwl_legacy_cancel_scan_deferred_work(priv);
}

static struct attribute *iwl3945_sysfs_entries[] = {
	&dev_attr_antenna.attr,
	&dev_attr_channels.attr,
	&dev_attr_dump_errors.attr,
	&dev_attr_flags.attr,
	&dev_attr_filter_flags.attr,
	&dev_attr_measurement.attr,
	&dev_attr_retry_rate.attr,
	&dev_attr_status.attr,
	&dev_attr_temperature.attr,
	&dev_attr_tx_power.attr,
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	&dev_attr_debug_level.attr,
#endif
	NULL
};

static struct attribute_group iwl3945_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = iwl3945_sysfs_entries,
};

struct ieee80211_ops iwl3945_hw_ops = {
	.tx = iwl3945_mac_tx,
	.start = iwl3945_mac_start,
	.stop = iwl3945_mac_stop,
	.add_interface = iwl_legacy_mac_add_interface,
	.remove_interface = iwl_legacy_mac_remove_interface,
	.change_interface = iwl_legacy_mac_change_interface,
	.config = iwl_legacy_mac_config,
	.configure_filter = iwl3945_configure_filter,
	.set_key = iwl3945_mac_set_key,
	.conf_tx = iwl_legacy_mac_conf_tx,
	.reset_tsf = iwl_legacy_mac_reset_tsf,
	.bss_info_changed = iwl_legacy_mac_bss_info_changed,
	.hw_scan = iwl_legacy_mac_hw_scan,
	.sta_add = iwl3945_mac_sta_add,
	.sta_remove = iwl_legacy_mac_sta_remove,
	.tx_last_beacon = iwl_legacy_mac_tx_last_beacon,
};

static int iwl3945_init_drv(struct iwl_priv *priv)
{
	int ret;
	struct iwl3945_eeprom *eeprom = (struct iwl3945_eeprom *)priv->eeprom;

	priv->retry_rate = 1;
	priv->beacon_skb = NULL;

	spin_lock_init(&priv->sta_lock);
	spin_lock_init(&priv->hcmd_lock);

	INIT_LIST_HEAD(&priv->free_frames);

	mutex_init(&priv->mutex);

	priv->ieee_channels = NULL;
	priv->ieee_rates = NULL;
	priv->band = IEEE80211_BAND_2GHZ;

	priv->iw_mode = NL80211_IFTYPE_STATION;
	priv->missed_beacon_threshold = IWL_MISSED_BEACON_THRESHOLD_DEF;

	/* initialize force reset */
	priv->force_reset[IWL_RF_RESET].reset_duration =
		IWL_DELAY_NEXT_FORCE_RF_RESET;
	priv->force_reset[IWL_FW_RESET].reset_duration =
		IWL_DELAY_NEXT_FORCE_FW_RELOAD;

	if (eeprom->version < EEPROM_3945_EEPROM_VERSION) {
		IWL_WARN(priv, "Unsupported EEPROM version: 0x%04X\n",
			 eeprom->version);
		ret = -EINVAL;
		goto err;
	}
	ret = iwl_legacy_init_channel_map(priv);
	if (ret) {
		IWL_ERR(priv, "initializing regulatory failed: %d\n", ret);
		goto err;
	}

	/* Set up txpower settings in driver for all channels */
	if (iwl3945_txpower_set_from_eeprom(priv)) {
		ret = -EIO;
		goto err_free_channel_map;
	}

	ret = iwl_legacy_init_geos(priv);
	if (ret) {
		IWL_ERR(priv, "initializing geos failed: %d\n", ret);
		goto err_free_channel_map;
	}
	iwl3945_init_hw_rates(priv, priv->ieee_rates);

	return 0;

err_free_channel_map:
	iwl_legacy_free_channel_map(priv);
err:
	return ret;
}

#define IWL3945_MAX_PROBE_REQUEST	200

static int iwl3945_setup_mac(struct iwl_priv *priv)
{
	int ret;
	struct ieee80211_hw *hw = priv->hw;

	hw->rate_control_algorithm = "iwl-3945-rs";
	hw->sta_data_size = sizeof(struct iwl3945_sta_priv);
	hw->vif_data_size = sizeof(struct iwl_vif_priv);

	/* Tell mac80211 our characteristics */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_SPECTRUM_MGMT;

	hw->wiphy->interface_modes =
		priv->contexts[IWL_RXON_CTX_BSS].interface_modes;

	hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY |
			    WIPHY_FLAG_DISABLE_BEACON_HINTS |
			    WIPHY_FLAG_IBSS_RSN;

	hw->wiphy->max_scan_ssids = PROBE_OPTION_MAX_3945;
	/* we create the 802.11 header and a zero-length SSID element */
	hw->wiphy->max_scan_ie_len = IWL3945_MAX_PROBE_REQUEST - 24 - 2;

	/* Default value; 4 EDCA QOS priorities */
	hw->queues = 4;

	if (priv->bands[IEEE80211_BAND_2GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&priv->bands[IEEE80211_BAND_2GHZ];

	if (priv->bands[IEEE80211_BAND_5GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&priv->bands[IEEE80211_BAND_5GHZ];

	iwl_legacy_leds_init(priv);

	ret = ieee80211_register_hw(priv->hw);
	if (ret) {
		IWL_ERR(priv, "Failed to register hw (error %d)\n", ret);
		return ret;
	}
	priv->mac80211_registered = 1;

	return 0;
}

static int iwl3945_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = 0, i;
	struct iwl_priv *priv;
	struct ieee80211_hw *hw;
	struct iwl_cfg *cfg = (struct iwl_cfg *)(ent->driver_data);
	struct iwl3945_eeprom *eeprom;
	unsigned long flags;

	/***********************
	 * 1. Allocating HW data
	 * ********************/

	/* mac80211 allocates memory for this device instance, including
	 *   space for this driver's private structure */
	hw = iwl_legacy_alloc_all(cfg);
	if (hw == NULL) {
		pr_err("Can not allocate network device\n");
		err = -ENOMEM;
		goto out;
	}
	priv = hw->priv;
	SET_IEEE80211_DEV(hw, &pdev->dev);

	priv->cmd_queue = IWL39_CMD_QUEUE_NUM;

	/* 3945 has only one valid context */
	priv->valid_contexts = BIT(IWL_RXON_CTX_BSS);

	for (i = 0; i < NUM_IWL_RXON_CTX; i++)
		priv->contexts[i].ctxid = i;

	priv->contexts[IWL_RXON_CTX_BSS].rxon_cmd = REPLY_RXON;
	priv->contexts[IWL_RXON_CTX_BSS].rxon_timing_cmd = REPLY_RXON_TIMING;
	priv->contexts[IWL_RXON_CTX_BSS].rxon_assoc_cmd = REPLY_RXON_ASSOC;
	priv->contexts[IWL_RXON_CTX_BSS].qos_cmd = REPLY_QOS_PARAM;
	priv->contexts[IWL_RXON_CTX_BSS].ap_sta_id = IWL_AP_ID;
	priv->contexts[IWL_RXON_CTX_BSS].wep_key_cmd = REPLY_WEPKEY;
	priv->contexts[IWL_RXON_CTX_BSS].interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);
	priv->contexts[IWL_RXON_CTX_BSS].ibss_devtype = RXON_DEV_TYPE_IBSS;
	priv->contexts[IWL_RXON_CTX_BSS].station_devtype = RXON_DEV_TYPE_ESS;
	priv->contexts[IWL_RXON_CTX_BSS].unused_devtype = RXON_DEV_TYPE_ESS;

	/*
	 * Disabling hardware scan means that mac80211 will perform scans
	 * "the hard way", rather than using device's scan.
	 */
	if (iwl3945_mod_params.disable_hw_scan) {
		IWL_DEBUG_INFO(priv, "Disabling hw_scan\n");
		iwl3945_hw_ops.hw_scan = NULL;
	}

	IWL_DEBUG_INFO(priv, "*** LOAD DRIVER ***\n");
	priv->cfg = cfg;
	priv->pci_dev = pdev;
	priv->inta_mask = CSR_INI_SET_MASK;

	if (iwl_legacy_alloc_traffic_mem(priv))
		IWL_ERR(priv, "Not enough memory to generate traffic log\n");

	/***************************
	 * 2. Initializing PCI bus
	 * *************************/
	pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1 |
				PCIE_LINK_STATE_CLKPM);

	if (pci_enable_device(pdev)) {
		err = -ENODEV;
		goto out_ieee80211_free_hw;
	}

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (err) {
		IWL_WARN(priv, "No suitable DMA available.\n");
		goto out_pci_disable_device;
	}

	pci_set_drvdata(pdev, priv);
	err = pci_request_regions(pdev, DRV_NAME);
	if (err)
		goto out_pci_disable_device;

	/***********************
	 * 3. Read REV Register
	 * ********************/
	priv->hw_base = pci_iomap(pdev, 0, 0);
	if (!priv->hw_base) {
		err = -ENODEV;
		goto out_pci_release_regions;
	}

	IWL_DEBUG_INFO(priv, "pci_resource_len = 0x%08llx\n",
			(unsigned long long) pci_resource_len(pdev, 0));
	IWL_DEBUG_INFO(priv, "pci_resource_base = %p\n", priv->hw_base);

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config_byte(pdev, 0x41, 0x00);

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

	/***********************
	 * 4. Read EEPROM
	 * ********************/

	/* Read the EEPROM */
	err = iwl_legacy_eeprom_init(priv);
	if (err) {
		IWL_ERR(priv, "Unable to init EEPROM\n");
		goto out_iounmap;
	}
	/* MAC Address location in EEPROM same for 3945/4965 */
	eeprom = (struct iwl3945_eeprom *)priv->eeprom;
	IWL_DEBUG_INFO(priv, "MAC address: %pM\n", eeprom->mac_address);
	SET_IEEE80211_PERM_ADDR(priv->hw, eeprom->mac_address);

	/***********************
	 * 5. Setup HW Constants
	 * ********************/
	/* Device-specific setup */
	if (iwl3945_hw_set_hw_params(priv)) {
		IWL_ERR(priv, "failed to set hw settings\n");
		goto out_eeprom_free;
	}

	/***********************
	 * 6. Setup priv
	 * ********************/

	err = iwl3945_init_drv(priv);
	if (err) {
		IWL_ERR(priv, "initializing driver failed\n");
		goto out_unset_hw_params;
	}

	IWL_INFO(priv, "Detected Intel Wireless WiFi Link %s\n",
		priv->cfg->name);

	/***********************
	 * 7. Setup Services
	 * ********************/

	spin_lock_irqsave(&priv->lock, flags);
	iwl_legacy_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	pci_enable_msi(priv->pci_dev);

	err = request_irq(priv->pci_dev->irq, iwl_legacy_isr,
			  IRQF_SHARED, DRV_NAME, priv);
	if (err) {
		IWL_ERR(priv, "Error allocating IRQ %d\n", priv->pci_dev->irq);
		goto out_disable_msi;
	}

	err = sysfs_create_group(&pdev->dev.kobj, &iwl3945_attribute_group);
	if (err) {
		IWL_ERR(priv, "failed to create sysfs device attributes\n");
		goto out_release_irq;
	}

	iwl_legacy_set_rxon_channel(priv,
			     &priv->bands[IEEE80211_BAND_2GHZ].channels[5],
			     &priv->contexts[IWL_RXON_CTX_BSS]);
	iwl3945_setup_deferred_work(priv);
	iwl3945_setup_rx_handlers(priv);
	iwl_legacy_power_initialize(priv);

	/*********************************
	 * 8. Setup and Register mac80211
	 * *******************************/

	iwl_legacy_enable_interrupts(priv);

	err = iwl3945_setup_mac(priv);
	if (err)
		goto  out_remove_sysfs;

	err = iwl_legacy_dbgfs_register(priv, DRV_NAME);
	if (err)
		IWL_ERR(priv, "failed to create debugfs files. Ignoring error: %d\n", err);

	/* Start monitoring the killswitch */
	queue_delayed_work(priv->workqueue, &priv->_3945.rfkill_poll,
			   2 * HZ);

	return 0;

 out_remove_sysfs:
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;
	sysfs_remove_group(&pdev->dev.kobj, &iwl3945_attribute_group);
 out_release_irq:
	free_irq(priv->pci_dev->irq, priv);
 out_disable_msi:
	pci_disable_msi(priv->pci_dev);
	iwl_legacy_free_geos(priv);
	iwl_legacy_free_channel_map(priv);
 out_unset_hw_params:
	iwl3945_unset_hw_params(priv);
 out_eeprom_free:
	iwl_legacy_eeprom_free(priv);
 out_iounmap:
	pci_iounmap(pdev, priv->hw_base);
 out_pci_release_regions:
	pci_release_regions(pdev);
 out_pci_disable_device:
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
 out_ieee80211_free_hw:
	iwl_legacy_free_traffic_mem(priv);
	ieee80211_free_hw(priv->hw);
 out:
	return err;
}

static void __devexit iwl3945_pci_remove(struct pci_dev *pdev)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);
	unsigned long flags;

	if (!priv)
		return;

	IWL_DEBUG_INFO(priv, "*** UNLOAD DRIVER ***\n");

	iwl_legacy_dbgfs_unregister(priv);

	set_bit(STATUS_EXIT_PENDING, &priv->status);

	iwl_legacy_leds_exit(priv);

	if (priv->mac80211_registered) {
		ieee80211_unregister_hw(priv->hw);
		priv->mac80211_registered = 0;
	} else {
		iwl3945_down(priv);
	}

	/*
	 * Make sure device is reset to low power before unloading driver.
	 * This may be redundant with iwl_down(), but there are paths to
	 * run iwl_down() without calling apm_ops.stop(), and there are
	 * paths to avoid running iwl_down() at all before leaving driver.
	 * This (inexpensive) call *makes sure* device is reset.
	 */
	iwl_legacy_apm_stop(priv);

	/* make sure we flush any pending irq or
	 * tasklet for the driver
	 */
	spin_lock_irqsave(&priv->lock, flags);
	iwl_legacy_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl3945_synchronize_irq(priv);

	sysfs_remove_group(&pdev->dev.kobj, &iwl3945_attribute_group);

	cancel_delayed_work_sync(&priv->_3945.rfkill_poll);

	iwl3945_dealloc_ucode_pci(priv);

	if (priv->rxq.bd)
		iwl3945_rx_queue_free(priv, &priv->rxq);
	iwl3945_hw_txq_ctx_free(priv);

	iwl3945_unset_hw_params(priv);

	/*netif_stop_queue(dev); */
	flush_workqueue(priv->workqueue);

	/* ieee80211_unregister_hw calls iwl3945_mac_stop, which flushes
	 * priv->workqueue... so we can't take down the workqueue
	 * until now... */
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;
	iwl_legacy_free_traffic_mem(priv);

	free_irq(pdev->irq, priv);
	pci_disable_msi(pdev);

	pci_iounmap(pdev, priv->hw_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	iwl_legacy_free_channel_map(priv);
	iwl_legacy_free_geos(priv);
	kfree(priv->scan_cmd);
	if (priv->beacon_skb)
		dev_kfree_skb(priv->beacon_skb);

	ieee80211_free_hw(priv->hw);
}


/*****************************************************************************
 *
 * driver and module entry point
 *
 *****************************************************************************/

static struct pci_driver iwl3945_driver = {
	.name = DRV_NAME,
	.id_table = iwl3945_hw_card_ids,
	.probe = iwl3945_pci_probe,
	.remove = __devexit_p(iwl3945_pci_remove),
	.driver.pm = IWL_LEGACY_PM_OPS,
};

static int __init iwl3945_init(void)
{

	int ret;
	pr_info(DRV_DESCRIPTION ", " DRV_VERSION "\n");
	pr_info(DRV_COPYRIGHT "\n");

	ret = iwl3945_rate_control_register();
	if (ret) {
		pr_err("Unable to register rate control algorithm: %d\n", ret);
		return ret;
	}

	ret = pci_register_driver(&iwl3945_driver);
	if (ret) {
		pr_err("Unable to initialize PCI module\n");
		goto error_register;
	}

	return ret;

error_register:
	iwl3945_rate_control_unregister();
	return ret;
}

static void __exit iwl3945_exit(void)
{
	pci_unregister_driver(&iwl3945_driver);
	iwl3945_rate_control_unregister();
}

MODULE_FIRMWARE(IWL3945_MODULE_FIRMWARE(IWL3945_UCODE_API_MAX));

module_param_named(antenna, iwl3945_mod_params.antenna, int, S_IRUGO);
MODULE_PARM_DESC(antenna, "select antenna (1=Main, 2=Aux, default 0 [both])");
module_param_named(swcrypto, iwl3945_mod_params.sw_crypto, int, S_IRUGO);
MODULE_PARM_DESC(swcrypto,
		"using software crypto (default 1 [software])");
module_param_named(disable_hw_scan, iwl3945_mod_params.disable_hw_scan,
		int, S_IRUGO);
MODULE_PARM_DESC(disable_hw_scan, "disable hardware scanning (default 1)");
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
module_param_named(debug, iwlegacy_debug_level, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "debug output mask");
#endif
module_param_named(fw_restart, iwl3945_mod_params.restart_fw, int, S_IRUGO);
MODULE_PARM_DESC(fw_restart, "restart firmware in case of error");

module_exit(iwl3945_exit);
module_init(iwl3945_init);
