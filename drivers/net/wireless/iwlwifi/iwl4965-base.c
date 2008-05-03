/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include <net/mac80211.h>

#include <asm/div64.h>

#include "iwl-eeprom.h"
#include "iwl-4965.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-sta.h"

static int iwl4965_tx_queue_update_write_ptr(struct iwl_priv *priv,
				  struct iwl4965_tx_queue *txq);

/******************************************************************************
 *
 * module boiler plate
 *
 ******************************************************************************/

/*
 * module name, copyright, version, etc.
 * NOTE: DRV_NAME is defined in iwlwifi.h for use by iwl-debug.h and printk
 */

#define DRV_DESCRIPTION	"Intel(R) Wireless WiFi Link 4965AGN driver for Linux"

#ifdef CONFIG_IWLWIFI_DEBUG
#define VD "d"
#else
#define VD
#endif

#ifdef CONFIG_IWL4965_SPECTRUM_MEASUREMENT
#define VS "s"
#else
#define VS
#endif

#define DRV_VERSION     IWLWIFI_VERSION VD VS


MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");

__le16 *ieee80211_get_qos_ctrl(struct ieee80211_hdr *hdr)
{
	u16 fc = le16_to_cpu(hdr->frame_control);
	int hdr_len = ieee80211_get_hdrlen(fc);

	if ((fc & 0x00cc) == (IEEE80211_STYPE_QOS_DATA | IEEE80211_FTYPE_DATA))
		return (__le16 *) ((u8 *) hdr + hdr_len - QOS_CONTROL_LEN);
	return NULL;
}

static const struct ieee80211_supported_band *iwl4965_get_hw_mode(
		struct iwl_priv *priv, enum ieee80211_band band)
{
	return priv->hw->wiphy->bands[band];
}

static int iwl4965_is_empty_essid(const char *essid, int essid_len)
{
	/* Single white space is for Linksys APs */
	if (essid_len == 1 && essid[0] == ' ')
		return 1;

	/* Otherwise, if the entire essid is 0, we assume it is hidden */
	while (essid_len) {
		essid_len--;
		if (essid[essid_len] != '\0')
			return 0;
	}

	return 1;
}

static const char *iwl4965_escape_essid(const char *essid, u8 essid_len)
{
	static char escaped[IW_ESSID_MAX_SIZE * 2 + 1];
	const char *s = essid;
	char *d = escaped;

	if (iwl4965_is_empty_essid(essid, essid_len)) {
		memcpy(escaped, "<hidden>", sizeof("<hidden>"));
		return escaped;
	}

	essid_len = min(essid_len, (u8) IW_ESSID_MAX_SIZE);
	while (essid_len--) {
		if (*s == '\0') {
			*d++ = '\\';
			*d++ = '0';
			s++;
		} else
			*d++ = *s++;
	}
	*d = '\0';
	return escaped;
}

/*************** DMA-QUEUE-GENERAL-FUNCTIONS  *****
 * DMA services
 *
 * Theory of operation
 *
 * A Tx or Rx queue resides in host DRAM, and is comprised of a circular buffer
 * of buffer descriptors, each of which points to one or more data buffers for
 * the device to read from or fill.  Driver and device exchange status of each
 * queue via "read" and "write" pointers.  Driver keeps minimum of 2 empty
 * entries in each circular buffer, to protect against confusing empty and full
 * queue states.
 *
 * The device reads or writes the data in the queues via the device's several
 * DMA/FIFO channels.  Each queue is mapped to a single DMA channel.
 *
 * For Tx queue, there are low mark and high mark limits. If, after queuing
 * the packet for Tx, free space become < low mark, Tx queue stopped. When
 * reclaiming packets (on 'tx done IRQ), if free space become > high mark,
 * Tx queue resumed.
 *
 * The 4965 operates with up to 17 queues:  One receive queue, one transmit
 * queue (#4) for sending commands to the device firmware, and 15 other
 * Tx queues that may be mapped to prioritized Tx DMA/FIFO channels.
 *
 * See more detailed info in iwl-4965-hw.h.
 ***************************************************/

int iwl4965_queue_space(const struct iwl4965_queue *q)
{
	int s = q->read_ptr - q->write_ptr;

	if (q->read_ptr > q->write_ptr)
		s -= q->n_bd;

	if (s <= 0)
		s += q->n_window;
	/* keep some reserve to not confuse empty and full situations */
	s -= 2;
	if (s < 0)
		s = 0;
	return s;
}


static inline int x2_queue_used(const struct iwl4965_queue *q, int i)
{
	return q->write_ptr > q->read_ptr ?
		(i >= q->read_ptr && i < q->write_ptr) :
		!(i < q->read_ptr && i >= q->write_ptr);
}

static inline u8 get_cmd_index(struct iwl4965_queue *q, u32 index, int is_huge)
{
	/* This is for scan command, the big buffer at end of command array */
	if (is_huge)
		return q->n_window;	/* must be power of 2 */

	/* Otherwise, use normal size buffers */
	return index & (q->n_window - 1);
}

/**
 * iwl4965_queue_init - Initialize queue's high/low-water and read/write indexes
 */
static int iwl4965_queue_init(struct iwl_priv *priv, struct iwl4965_queue *q,
			  int count, int slots_num, u32 id)
{
	q->n_bd = count;
	q->n_window = slots_num;
	q->id = id;

	/* count must be power-of-two size, otherwise iwl_queue_inc_wrap
	 * and iwl_queue_dec_wrap are broken. */
	BUG_ON(!is_power_of_2(count));

	/* slots_num must be power-of-two size, otherwise
	 * get_cmd_index is broken. */
	BUG_ON(!is_power_of_2(slots_num));

	q->low_mark = q->n_window / 4;
	if (q->low_mark < 4)
		q->low_mark = 4;

	q->high_mark = q->n_window / 8;
	if (q->high_mark < 2)
		q->high_mark = 2;

	q->write_ptr = q->read_ptr = 0;

	return 0;
}

/**
 * iwl4965_tx_queue_alloc - Alloc driver data and TFD CB for one Tx/cmd queue
 */
static int iwl4965_tx_queue_alloc(struct iwl_priv *priv,
			      struct iwl4965_tx_queue *txq, u32 id)
{
	struct pci_dev *dev = priv->pci_dev;

	/* Driver private data, only for Tx (not command) queues,
	 * not shared with device. */
	if (id != IWL_CMD_QUEUE_NUM) {
		txq->txb = kmalloc(sizeof(txq->txb[0]) *
				   TFD_QUEUE_SIZE_MAX, GFP_KERNEL);
		if (!txq->txb) {
			IWL_ERROR("kmalloc for auxiliary BD "
				  "structures failed\n");
			goto error;
		}
	} else
		txq->txb = NULL;

	/* Circular buffer of transmit frame descriptors (TFDs),
	 * shared with device */
	txq->bd = pci_alloc_consistent(dev,
			sizeof(txq->bd[0]) * TFD_QUEUE_SIZE_MAX,
			&txq->q.dma_addr);

	if (!txq->bd) {
		IWL_ERROR("pci_alloc_consistent(%zd) failed\n",
			  sizeof(txq->bd[0]) * TFD_QUEUE_SIZE_MAX);
		goto error;
	}
	txq->q.id = id;

	return 0;

 error:
	if (txq->txb) {
		kfree(txq->txb);
		txq->txb = NULL;
	}

	return -ENOMEM;
}

/**
 * iwl4965_tx_queue_init - Allocate and initialize one tx/cmd queue
 */
int iwl4965_tx_queue_init(struct iwl_priv *priv,
		      struct iwl4965_tx_queue *txq, int slots_num, u32 txq_id)
{
	struct pci_dev *dev = priv->pci_dev;
	int len;
	int rc = 0;

	/*
	 * Alloc buffer array for commands (Tx or other types of commands).
	 * For the command queue (#4), allocate command space + one big
	 * command for scan, since scan command is very huge; the system will
	 * not have two scans at the same time, so only one is needed.
	 * For normal Tx queues (all other queues), no super-size command
	 * space is needed.
	 */
	len = sizeof(struct iwl_cmd) * slots_num;
	if (txq_id == IWL_CMD_QUEUE_NUM)
		len +=  IWL_MAX_SCAN_SIZE;
	txq->cmd = pci_alloc_consistent(dev, len, &txq->dma_addr_cmd);
	if (!txq->cmd)
		return -ENOMEM;

	/* Alloc driver data array and TFD circular buffer */
	rc = iwl4965_tx_queue_alloc(priv, txq, txq_id);
	if (rc) {
		pci_free_consistent(dev, len, txq->cmd, txq->dma_addr_cmd);

		return -ENOMEM;
	}
	txq->need_update = 0;

	/* TFD_QUEUE_SIZE_MAX must be power-of-two size, otherwise
	 * iwl_queue_inc_wrap and iwl_queue_dec_wrap are broken. */
	BUILD_BUG_ON(TFD_QUEUE_SIZE_MAX & (TFD_QUEUE_SIZE_MAX - 1));

	/* Initialize queue's high/low-water marks, and head/tail indexes */
	iwl4965_queue_init(priv, &txq->q, TFD_QUEUE_SIZE_MAX, slots_num, txq_id);

	/* Tell device where to find queue */
	iwl4965_hw_tx_queue_init(priv, txq);

	return 0;
}

/**
 * iwl4965_tx_queue_free - Deallocate DMA queue.
 * @txq: Transmit queue to deallocate.
 *
 * Empty queue by removing and destroying all BD's.
 * Free all buffers.
 * 0-fill, but do not free "txq" descriptor structure.
 */
void iwl4965_tx_queue_free(struct iwl_priv *priv, struct iwl4965_tx_queue *txq)
{
	struct iwl4965_queue *q = &txq->q;
	struct pci_dev *dev = priv->pci_dev;
	int len;

	if (q->n_bd == 0)
		return;

	/* first, empty all BD's */
	for (; q->write_ptr != q->read_ptr;
	     q->read_ptr = iwl_queue_inc_wrap(q->read_ptr, q->n_bd))
		iwl4965_hw_txq_free_tfd(priv, txq);

	len = sizeof(struct iwl_cmd) * q->n_window;
	if (q->id == IWL_CMD_QUEUE_NUM)
		len += IWL_MAX_SCAN_SIZE;

	/* De-alloc array of command/tx buffers */
	pci_free_consistent(dev, len, txq->cmd, txq->dma_addr_cmd);

	/* De-alloc circular buffer of TFDs */
	if (txq->q.n_bd)
		pci_free_consistent(dev, sizeof(struct iwl4965_tfd_frame) *
				    txq->q.n_bd, txq->bd, txq->q.dma_addr);

	/* De-alloc array of per-TFD driver data */
	if (txq->txb) {
		kfree(txq->txb);
		txq->txb = NULL;
	}

	/* 0-fill queue descriptor structure */
	memset(txq, 0, sizeof(*txq));
}

const u8 iwl4965_broadcast_addr[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/*************** STATION TABLE MANAGEMENT ****
 * mac80211 should be examined to determine if sta_info is duplicating
 * the functionality provided here
 */

/**************************************************************/

#if 0 /* temporary disable till we add real remove station */
/**
 * iwl4965_remove_station - Remove driver's knowledge of station.
 *
 * NOTE:  This does not remove station from device's station table.
 */
static u8 iwl4965_remove_station(struct iwl_priv *priv, const u8 *addr, int is_ap)
{
	int index = IWL_INVALID_STATION;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);

	if (is_ap)
		index = IWL_AP_ID;
	else if (is_broadcast_ether_addr(addr))
		index = priv->hw_params.bcast_sta_id;
	else
		for (i = IWL_STA_ID; i < priv->hw_params.max_stations; i++)
			if (priv->stations[i].used &&
			    !compare_ether_addr(priv->stations[i].sta.sta.addr,
						addr)) {
				index = i;
				break;
			}

	if (unlikely(index == IWL_INVALID_STATION))
		goto out;

	if (priv->stations[index].used) {
		priv->stations[index].used = 0;
		priv->num_stations--;
	}

	BUG_ON(priv->num_stations < 0);

out:
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return 0;
}
#endif

/**
 * iwl4965_add_station_flags - Add station to tables in driver and device
 */
u8 iwl4965_add_station_flags(struct iwl_priv *priv, const u8 *addr,
				int is_ap, u8 flags, void *ht_data)
{
	int i;
	int index = IWL_INVALID_STATION;
	struct iwl4965_station_entry *station;
	unsigned long flags_spin;
	DECLARE_MAC_BUF(mac);

	spin_lock_irqsave(&priv->sta_lock, flags_spin);
	if (is_ap)
		index = IWL_AP_ID;
	else if (is_broadcast_ether_addr(addr))
		index = priv->hw_params.bcast_sta_id;
	else
		for (i = IWL_STA_ID; i < priv->hw_params.max_stations; i++) {
			if (!compare_ether_addr(priv->stations[i].sta.sta.addr,
						addr)) {
				index = i;
				break;
			}

			if (!priv->stations[i].used &&
			    index == IWL_INVALID_STATION)
				index = i;
		}


	/* These two conditions have the same outcome, but keep them separate
	  since they have different meanings */
	if (unlikely(index == IWL_INVALID_STATION)) {
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return index;
	}

	if (priv->stations[index].used &&
	    !compare_ether_addr(priv->stations[index].sta.sta.addr, addr)) {
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return index;
	}


	IWL_DEBUG_ASSOC("Add STA ID %d: %s\n", index, print_mac(mac, addr));
	station = &priv->stations[index];
	station->used = 1;
	priv->num_stations++;

	/* Set up the REPLY_ADD_STA command to send to device */
	memset(&station->sta, 0, sizeof(struct iwl4965_addsta_cmd));
	memcpy(station->sta.sta.addr, addr, ETH_ALEN);
	station->sta.mode = 0;
	station->sta.sta.sta_id = index;
	station->sta.station_flags = 0;

#ifdef CONFIG_IWL4965_HT
	/* BCAST station and IBSS stations do not work in HT mode */
	if (index != priv->hw_params.bcast_sta_id &&
	    priv->iw_mode != IEEE80211_IF_TYPE_IBSS)
		iwl4965_set_ht_add_station(priv, index,
				 (struct ieee80211_ht_info *) ht_data);
#endif /*CONFIG_IWL4965_HT*/

	spin_unlock_irqrestore(&priv->sta_lock, flags_spin);

	/* Add station to device's station table */
	iwl4965_send_add_station(priv, &station->sta, flags);
	return index;

}



/*************** HOST COMMAND QUEUE FUNCTIONS   *****/

/**
 * iwl4965_enqueue_hcmd - enqueue a uCode command
 * @priv: device private data point
 * @cmd: a point to the ucode command structure
 *
 * The function returns < 0 values to indicate the operation is
 * failed. On success, it turns the index (> 0) of command in the
 * command queue.
 */
int iwl4965_enqueue_hcmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	struct iwl4965_tx_queue *txq = &priv->txq[IWL_CMD_QUEUE_NUM];
	struct iwl4965_queue *q = &txq->q;
	struct iwl4965_tfd_frame *tfd;
	u32 *control_flags;
	struct iwl_cmd *out_cmd;
	u32 idx;
	u16 fix_size = (u16)(cmd->len + sizeof(out_cmd->hdr));
	dma_addr_t phys_addr;
	int ret;
	unsigned long flags;

	/* If any of the command structures end up being larger than
	 * the TFD_MAX_PAYLOAD_SIZE, and it sent as a 'small' command then
	 * we will need to increase the size of the TFD entries */
	BUG_ON((fix_size > TFD_MAX_PAYLOAD_SIZE) &&
	       !(cmd->meta.flags & CMD_SIZE_HUGE));

	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_INFO("Not sending command - RF KILL");
		return -EIO;
	}

	if (iwl4965_queue_space(q) < ((cmd->meta.flags & CMD_ASYNC) ? 2 : 1)) {
		IWL_ERROR("No space for Tx\n");
		return -ENOSPC;
	}

	spin_lock_irqsave(&priv->hcmd_lock, flags);

	tfd = &txq->bd[q->write_ptr];
	memset(tfd, 0, sizeof(*tfd));

	control_flags = (u32 *) tfd;

	idx = get_cmd_index(q, q->write_ptr, cmd->meta.flags & CMD_SIZE_HUGE);
	out_cmd = &txq->cmd[idx];

	out_cmd->hdr.cmd = cmd->id;
	memcpy(&out_cmd->meta, &cmd->meta, sizeof(cmd->meta));
	memcpy(&out_cmd->cmd.payload, cmd->data, cmd->len);

	/* At this point, the out_cmd now has all of the incoming cmd
	 * information */

	out_cmd->hdr.flags = 0;
	out_cmd->hdr.sequence = cpu_to_le16(QUEUE_TO_SEQ(IWL_CMD_QUEUE_NUM) |
			INDEX_TO_SEQ(q->write_ptr));
	if (out_cmd->meta.flags & CMD_SIZE_HUGE)
		out_cmd->hdr.sequence |= cpu_to_le16(SEQ_HUGE_FRAME);

	phys_addr = txq->dma_addr_cmd + sizeof(txq->cmd[0]) * idx +
			offsetof(struct iwl_cmd, hdr);
	iwl4965_hw_txq_attach_buf_to_tfd(priv, tfd, phys_addr, fix_size);

	IWL_DEBUG_HC("Sending command %s (#%x), seq: 0x%04X, "
		     "%d bytes at %d[%d]:%d\n",
		     get_cmd_string(out_cmd->hdr.cmd),
		     out_cmd->hdr.cmd, le16_to_cpu(out_cmd->hdr.sequence),
		     fix_size, q->write_ptr, idx, IWL_CMD_QUEUE_NUM);

	txq->need_update = 1;

	/* Set up entry in queue's byte count circular buffer */
	priv->cfg->ops->lib->txq_update_byte_cnt_tbl(priv, txq, 0);

	/* Increment and update queue's write index */
	q->write_ptr = iwl_queue_inc_wrap(q->write_ptr, q->n_bd);
	ret = iwl4965_tx_queue_update_write_ptr(priv, txq);

	spin_unlock_irqrestore(&priv->hcmd_lock, flags);
	return ret ? ret : idx;
}

static void iwl4965_set_rxon_hwcrypto(struct iwl_priv *priv, int hw_decrypt)
{
	struct iwl4965_rxon_cmd *rxon = &priv->staging_rxon;

	if (hw_decrypt)
		rxon->filter_flags &= ~RXON_FILTER_DIS_DECRYPT_MSK;
	else
		rxon->filter_flags |= RXON_FILTER_DIS_DECRYPT_MSK;

}

/**
 * iwl4965_rxon_add_station - add station into station table.
 *
 * there is only one AP station with id= IWL_AP_ID
 * NOTE: mutex must be held before calling this fnction
 */
static int iwl4965_rxon_add_station(struct iwl_priv *priv,
				const u8 *addr, int is_ap)
{
	u8 sta_id;

	/* Add station to device's station table */
#ifdef CONFIG_IWL4965_HT
	struct ieee80211_conf *conf = &priv->hw->conf;
	struct ieee80211_ht_info *cur_ht_config = &conf->ht_conf;

	if ((is_ap) &&
	    (conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE) &&
	    (priv->iw_mode == IEEE80211_IF_TYPE_STA))
		sta_id = iwl4965_add_station_flags(priv, addr, is_ap,
						   0, cur_ht_config);
	else
#endif /* CONFIG_IWL4965_HT */
		sta_id = iwl4965_add_station_flags(priv, addr, is_ap,
						   0, NULL);

	/* Set up default rate scaling table in device's station table */
	iwl4965_add_station(priv, addr, is_ap);

	return sta_id;
}

/**
 * iwl4965_check_rxon_cmd - validate RXON structure is valid
 *
 * NOTE:  This is really only useful during development and can eventually
 * be #ifdef'd out once the driver is stable and folks aren't actively
 * making changes
 */
static int iwl4965_check_rxon_cmd(struct iwl4965_rxon_cmd *rxon)
{
	int error = 0;
	int counter = 1;

	if (rxon->flags & RXON_FLG_BAND_24G_MSK) {
		error |= le32_to_cpu(rxon->flags &
				(RXON_FLG_TGJ_NARROW_BAND_MSK |
				 RXON_FLG_RADAR_DETECT_MSK));
		if (error)
			IWL_WARNING("check 24G fields %d | %d\n",
				    counter++, error);
	} else {
		error |= (rxon->flags & RXON_FLG_SHORT_SLOT_MSK) ?
				0 : le32_to_cpu(RXON_FLG_SHORT_SLOT_MSK);
		if (error)
			IWL_WARNING("check 52 fields %d | %d\n",
				    counter++, error);
		error |= le32_to_cpu(rxon->flags & RXON_FLG_CCK_MSK);
		if (error)
			IWL_WARNING("check 52 CCK %d | %d\n",
				    counter++, error);
	}
	error |= (rxon->node_addr[0] | rxon->bssid_addr[0]) & 0x1;
	if (error)
		IWL_WARNING("check mac addr %d | %d\n", counter++, error);

	/* make sure basic rates 6Mbps and 1Mbps are supported */
	error |= (((rxon->ofdm_basic_rates & IWL_RATE_6M_MASK) == 0) &&
		  ((rxon->cck_basic_rates & IWL_RATE_1M_MASK) == 0));
	if (error)
		IWL_WARNING("check basic rate %d | %d\n", counter++, error);

	error |= (le16_to_cpu(rxon->assoc_id) > 2007);
	if (error)
		IWL_WARNING("check assoc id %d | %d\n", counter++, error);

	error |= ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK))
			== (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK));
	if (error)
		IWL_WARNING("check CCK and short slot %d | %d\n",
			    counter++, error);

	error |= ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK))
			== (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK));
	if (error)
		IWL_WARNING("check CCK & auto detect %d | %d\n",
			    counter++, error);

	error |= ((rxon->flags & (RXON_FLG_AUTO_DETECT_MSK |
			RXON_FLG_TGG_PROTECT_MSK)) == RXON_FLG_TGG_PROTECT_MSK);
	if (error)
		IWL_WARNING("check TGG and auto detect %d | %d\n",
			    counter++, error);

	if (error)
		IWL_WARNING("Tuning to channel %d\n",
			    le16_to_cpu(rxon->channel));

	if (error) {
		IWL_ERROR("Not a valid iwl4965_rxon_assoc_cmd field values\n");
		return -1;
	}
	return 0;
}

/**
 * iwl4965_full_rxon_required - check if full RXON (vs RXON_ASSOC) cmd is needed
 * @priv: staging_rxon is compared to active_rxon
 *
 * If the RXON structure is changing enough to require a new tune,
 * or is clearing the RXON_FILTER_ASSOC_MSK, then return 1 to indicate that
 * a new tune (full RXON command, rather than RXON_ASSOC cmd) is required.
 */
static int iwl4965_full_rxon_required(struct iwl_priv *priv)
{

	/* These items are only settable from the full RXON command */
	if (!(priv->active_rxon.filter_flags & RXON_FILTER_ASSOC_MSK) ||
	    compare_ether_addr(priv->staging_rxon.bssid_addr,
			       priv->active_rxon.bssid_addr) ||
	    compare_ether_addr(priv->staging_rxon.node_addr,
			       priv->active_rxon.node_addr) ||
	    compare_ether_addr(priv->staging_rxon.wlap_bssid_addr,
			       priv->active_rxon.wlap_bssid_addr) ||
	    (priv->staging_rxon.dev_type != priv->active_rxon.dev_type) ||
	    (priv->staging_rxon.channel != priv->active_rxon.channel) ||
	    (priv->staging_rxon.air_propagation !=
	     priv->active_rxon.air_propagation) ||
	    (priv->staging_rxon.ofdm_ht_single_stream_basic_rates !=
	     priv->active_rxon.ofdm_ht_single_stream_basic_rates) ||
	    (priv->staging_rxon.ofdm_ht_dual_stream_basic_rates !=
	     priv->active_rxon.ofdm_ht_dual_stream_basic_rates) ||
	    (priv->staging_rxon.rx_chain != priv->active_rxon.rx_chain) ||
	    (priv->staging_rxon.assoc_id != priv->active_rxon.assoc_id))
		return 1;

	/* flags, filter_flags, ofdm_basic_rates, and cck_basic_rates can
	 * be updated with the RXON_ASSOC command -- however only some
	 * flag transitions are allowed using RXON_ASSOC */

	/* Check if we are not switching bands */
	if ((priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK) !=
	    (priv->active_rxon.flags & RXON_FLG_BAND_24G_MSK))
		return 1;

	/* Check if we are switching association toggle */
	if ((priv->staging_rxon.filter_flags & RXON_FILTER_ASSOC_MSK) !=
		(priv->active_rxon.filter_flags & RXON_FILTER_ASSOC_MSK))
		return 1;

	return 0;
}

/**
 * iwl4965_commit_rxon - commit staging_rxon to hardware
 *
 * The RXON command in staging_rxon is committed to the hardware and
 * the active_rxon structure is updated with the new data.  This
 * function correctly transitions out of the RXON_ASSOC_MSK state if
 * a HW tune is required based on the RXON structure changes.
 */
static int iwl4965_commit_rxon(struct iwl_priv *priv)
{
	/* cast away the const for active_rxon in this function */
	struct iwl4965_rxon_cmd *active_rxon = (void *)&priv->active_rxon;
	DECLARE_MAC_BUF(mac);
	int rc = 0;

	if (!iwl_is_alive(priv))
		return -1;

	/* always get timestamp with Rx frame */
	priv->staging_rxon.flags |= RXON_FLG_TSF2HOST_MSK;

	rc = iwl4965_check_rxon_cmd(&priv->staging_rxon);
	if (rc) {
		IWL_ERROR("Invalid RXON configuration.  Not committing.\n");
		return -EINVAL;
	}

	/* If we don't need to send a full RXON, we can use
	 * iwl4965_rxon_assoc_cmd which is used to reconfigure filter
	 * and other flags for the current radio configuration. */
	if (!iwl4965_full_rxon_required(priv)) {
		rc = iwl_send_rxon_assoc(priv);
		if (rc) {
			IWL_ERROR("Error setting RXON_ASSOC "
				  "configuration (%d).\n", rc);
			return rc;
		}

		memcpy(active_rxon, &priv->staging_rxon, sizeof(*active_rxon));

		return 0;
	}

	/* station table will be cleared */
	priv->assoc_station_added = 0;

#ifdef CONFIG_IWL4965_SENSITIVITY
	priv->sensitivity_data.state = IWL_SENS_CALIB_NEED_REINIT;
	if (!priv->error_recovering)
		priv->start_calib = 0;

	iwl4965_init_sensitivity(priv, CMD_ASYNC, 1);
#endif /* CONFIG_IWL4965_SENSITIVITY */

	/* If we are currently associated and the new config requires
	 * an RXON_ASSOC and the new config wants the associated mask enabled,
	 * we must clear the associated from the active configuration
	 * before we apply the new config */
	if (iwl_is_associated(priv) &&
	    (priv->staging_rxon.filter_flags & RXON_FILTER_ASSOC_MSK)) {
		IWL_DEBUG_INFO("Toggling associated bit on current RXON\n");
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;

		rc = iwl_send_cmd_pdu(priv, REPLY_RXON,
				      sizeof(struct iwl4965_rxon_cmd),
				      &priv->active_rxon);

		/* If the mask clearing failed then we set
		 * active_rxon back to what it was previously */
		if (rc) {
			active_rxon->filter_flags |= RXON_FILTER_ASSOC_MSK;
			IWL_ERROR("Error clearing ASSOC_MSK on current "
				  "configuration (%d).\n", rc);
			return rc;
		}
	}

	IWL_DEBUG_INFO("Sending RXON\n"
		       "* with%s RXON_FILTER_ASSOC_MSK\n"
		       "* channel = %d\n"
		       "* bssid = %s\n",
		       ((priv->staging_rxon.filter_flags &
			 RXON_FILTER_ASSOC_MSK) ? "" : "out"),
		       le16_to_cpu(priv->staging_rxon.channel),
		       print_mac(mac, priv->staging_rxon.bssid_addr));

	iwl4965_set_rxon_hwcrypto(priv, !priv->cfg->mod_params->sw_crypto);
	/* Apply the new configuration */
	rc = iwl_send_cmd_pdu(priv, REPLY_RXON,
			      sizeof(struct iwl4965_rxon_cmd), &priv->staging_rxon);
	if (rc) {
		IWL_ERROR("Error setting new configuration (%d).\n", rc);
		return rc;
	}

	iwlcore_clear_stations_table(priv);

#ifdef CONFIG_IWL4965_SENSITIVITY
	if (!priv->error_recovering)
		priv->start_calib = 0;

	priv->sensitivity_data.state = IWL_SENS_CALIB_NEED_REINIT;
	iwl4965_init_sensitivity(priv, CMD_ASYNC, 1);
#endif /* CONFIG_IWL4965_SENSITIVITY */

	memcpy(active_rxon, &priv->staging_rxon, sizeof(*active_rxon));

	/* If we issue a new RXON command which required a tune then we must
	 * send a new TXPOWER command or we won't be able to Tx any frames */
	rc = iwl4965_hw_reg_send_txpower(priv);
	if (rc) {
		IWL_ERROR("Error setting Tx power (%d).\n", rc);
		return rc;
	}

	/* Add the broadcast address so we can send broadcast frames */
	if (iwl4965_rxon_add_station(priv, iwl4965_broadcast_addr, 0) ==
	    IWL_INVALID_STATION) {
		IWL_ERROR("Error adding BROADCAST address for transmit.\n");
		return -EIO;
	}

	/* If we have set the ASSOC_MSK and we are in BSS mode then
	 * add the IWL_AP_ID to the station rate table */
	if (iwl_is_associated(priv) &&
	    (priv->iw_mode == IEEE80211_IF_TYPE_STA)) {
		if (iwl4965_rxon_add_station(priv, priv->active_rxon.bssid_addr, 1)
		    == IWL_INVALID_STATION) {
			IWL_ERROR("Error adding AP address for transmit.\n");
			return -EIO;
		}
		priv->assoc_station_added = 1;
		if (priv->default_wep_key &&
		    iwl_send_static_wepkey_cmd(priv, 0))
			IWL_ERROR("Could not send WEP static key.\n");
	}

	return 0;
}

static int iwl4965_send_bt_config(struct iwl_priv *priv)
{
	struct iwl4965_bt_cmd bt_cmd = {
		.flags = 3,
		.lead_time = 0xAA,
		.max_kill = 1,
		.kill_ack_mask = 0,
		.kill_cts_mask = 0,
	};

	return iwl_send_cmd_pdu(priv, REPLY_BT_CONFIG,
				sizeof(struct iwl4965_bt_cmd), &bt_cmd);
}

static int iwl4965_send_scan_abort(struct iwl_priv *priv)
{
	int rc = 0;
	struct iwl4965_rx_packet *res;
	struct iwl_host_cmd cmd = {
		.id = REPLY_SCAN_ABORT_CMD,
		.meta.flags = CMD_WANT_SKB,
	};

	/* If there isn't a scan actively going on in the hardware
	 * then we are in between scan bands and not actually
	 * actively scanning, so don't send the abort command */
	if (!test_bit(STATUS_SCAN_HW, &priv->status)) {
		clear_bit(STATUS_SCAN_ABORTING, &priv->status);
		return 0;
	}

	rc = iwl_send_cmd_sync(priv, &cmd);
	if (rc) {
		clear_bit(STATUS_SCAN_ABORTING, &priv->status);
		return rc;
	}

	res = (struct iwl4965_rx_packet *)cmd.meta.u.skb->data;
	if (res->u.status != CAN_ABORT_STATUS) {
		/* The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before we
		 * the microcode has notified us that a scan is
		 * completed. */
		IWL_DEBUG_INFO("SCAN_ABORT returned %d.\n", res->u.status);
		clear_bit(STATUS_SCAN_ABORTING, &priv->status);
		clear_bit(STATUS_SCAN_HW, &priv->status);
	}

	dev_kfree_skb_any(cmd.meta.u.skb);

	return rc;
}

static int iwl4965_card_state_sync_callback(struct iwl_priv *priv,
					struct iwl_cmd *cmd,
					struct sk_buff *skb)
{
	return 1;
}

/*
 * CARD_STATE_CMD
 *
 * Use: Sets the device's internal card state to enable, disable, or halt
 *
 * When in the 'enable' state the card operates as normal.
 * When in the 'disable' state, the card enters into a low power mode.
 * When in the 'halt' state, the card is shut down and must be fully
 * restarted to come back on.
 */
static int iwl4965_send_card_state(struct iwl_priv *priv, u32 flags, u8 meta_flag)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_CARD_STATE_CMD,
		.len = sizeof(u32),
		.data = &flags,
		.meta.flags = meta_flag,
	};

	if (meta_flag & CMD_ASYNC)
		cmd.meta.u.callback = iwl4965_card_state_sync_callback;

	return iwl_send_cmd(priv, &cmd);
}

static int iwl4965_add_sta_sync_callback(struct iwl_priv *priv,
				     struct iwl_cmd *cmd, struct sk_buff *skb)
{
	struct iwl4965_rx_packet *res = NULL;

	if (!skb) {
		IWL_ERROR("Error: Response NULL in REPLY_ADD_STA.\n");
		return 1;
	}

	res = (struct iwl4965_rx_packet *)skb->data;
	if (res->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERROR("Bad return from REPLY_ADD_STA (0x%08X)\n",
			  res->hdr.flags);
		return 1;
	}

	switch (res->u.add_sta.status) {
	case ADD_STA_SUCCESS_MSK:
		break;
	default:
		break;
	}

	/* We didn't cache the SKB; let the caller free it */
	return 1;
}

int iwl4965_send_add_station(struct iwl_priv *priv,
			 struct iwl4965_addsta_cmd *sta, u8 flags)
{
	struct iwl4965_rx_packet *res = NULL;
	int rc = 0;
	struct iwl_host_cmd cmd = {
		.id = REPLY_ADD_STA,
		.len = sizeof(struct iwl4965_addsta_cmd),
		.meta.flags = flags,
		.data = sta,
	};

	if (flags & CMD_ASYNC)
		cmd.meta.u.callback = iwl4965_add_sta_sync_callback;
	else
		cmd.meta.flags |= CMD_WANT_SKB;

	rc = iwl_send_cmd(priv, &cmd);

	if (rc || (flags & CMD_ASYNC))
		return rc;

	res = (struct iwl4965_rx_packet *)cmd.meta.u.skb->data;
	if (res->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERROR("Bad return from REPLY_ADD_STA (0x%08X)\n",
			  res->hdr.flags);
		rc = -EIO;
	}

	if (rc == 0) {
		switch (res->u.add_sta.status) {
		case ADD_STA_SUCCESS_MSK:
			IWL_DEBUG_INFO("REPLY_ADD_STA PASSED\n");
			break;
		default:
			rc = -EIO;
			IWL_WARNING("REPLY_ADD_STA failed\n");
			break;
		}
	}

	priv->alloc_rxb_skb--;
	dev_kfree_skb_any(cmd.meta.u.skb);

	return rc;
}

static void iwl4965_clear_free_frames(struct iwl_priv *priv)
{
	struct list_head *element;

	IWL_DEBUG_INFO("%d frames on pre-allocated heap on clear.\n",
		       priv->frames_count);

	while (!list_empty(&priv->free_frames)) {
		element = priv->free_frames.next;
		list_del(element);
		kfree(list_entry(element, struct iwl4965_frame, list));
		priv->frames_count--;
	}

	if (priv->frames_count) {
		IWL_WARNING("%d frames still in use.  Did we lose one?\n",
			    priv->frames_count);
		priv->frames_count = 0;
	}
}

static struct iwl4965_frame *iwl4965_get_free_frame(struct iwl_priv *priv)
{
	struct iwl4965_frame *frame;
	struct list_head *element;
	if (list_empty(&priv->free_frames)) {
		frame = kzalloc(sizeof(*frame), GFP_KERNEL);
		if (!frame) {
			IWL_ERROR("Could not allocate frame!\n");
			return NULL;
		}

		priv->frames_count++;
		return frame;
	}

	element = priv->free_frames.next;
	list_del(element);
	return list_entry(element, struct iwl4965_frame, list);
}

static void iwl4965_free_frame(struct iwl_priv *priv, struct iwl4965_frame *frame)
{
	memset(frame, 0, sizeof(*frame));
	list_add(&frame->list, &priv->free_frames);
}

unsigned int iwl4965_fill_beacon_frame(struct iwl_priv *priv,
				struct ieee80211_hdr *hdr,
				const u8 *dest, int left)
{

	if (!iwl_is_associated(priv) || !priv->ibss_beacon ||
	    ((priv->iw_mode != IEEE80211_IF_TYPE_IBSS) &&
	     (priv->iw_mode != IEEE80211_IF_TYPE_AP)))
		return 0;

	if (priv->ibss_beacon->len > left)
		return 0;

	memcpy(hdr, priv->ibss_beacon->data, priv->ibss_beacon->len);

	return priv->ibss_beacon->len;
}

static u8 iwl4965_rate_get_lowest_plcp(int rate_mask)
{
	u8 i;

	for (i = IWL_RATE_1M_INDEX; i != IWL_RATE_INVALID;
	     i = iwl4965_rates[i].next_ieee) {
		if (rate_mask & (1 << i))
			return iwl4965_rates[i].plcp;
	}

	return IWL_RATE_INVALID;
}

static int iwl4965_send_beacon_cmd(struct iwl_priv *priv)
{
	struct iwl4965_frame *frame;
	unsigned int frame_size;
	int rc;
	u8 rate;

	frame = iwl4965_get_free_frame(priv);

	if (!frame) {
		IWL_ERROR("Could not obtain free frame buffer for beacon "
			  "command.\n");
		return -ENOMEM;
	}

	if (!(priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK)) {
		rate = iwl4965_rate_get_lowest_plcp(priv->active_rate_basic &
						0xFF0);
		if (rate == IWL_INVALID_RATE)
			rate = IWL_RATE_6M_PLCP;
	} else {
		rate = iwl4965_rate_get_lowest_plcp(priv->active_rate_basic & 0xF);
		if (rate == IWL_INVALID_RATE)
			rate = IWL_RATE_1M_PLCP;
	}

	frame_size = iwl4965_hw_get_beacon_cmd(priv, frame, rate);

	rc = iwl_send_cmd_pdu(priv, REPLY_TX_BEACON, frame_size,
			      &frame->u.cmd[0]);

	iwl4965_free_frame(priv, frame);

	return rc;
}

/******************************************************************************
 *
 * Misc. internal state and helper functions
 *
 ******************************************************************************/

static void iwl4965_unset_hw_params(struct iwl_priv *priv)
{
	if (priv->shared_virt)
		pci_free_consistent(priv->pci_dev,
				    sizeof(struct iwl4965_shared),
				    priv->shared_virt,
				    priv->shared_phys);
}

/**
 * iwl4965_supported_rate_to_ie - fill in the supported rate in IE field
 *
 * return : set the bit for each supported rate insert in ie
 */
static u16 iwl4965_supported_rate_to_ie(u8 *ie, u16 supported_rate,
				    u16 basic_rate, int *left)
{
	u16 ret_rates = 0, bit;
	int i;
	u8 *cnt = ie;
	u8 *rates = ie + 1;

	for (bit = 1, i = 0; i < IWL_RATE_COUNT; i++, bit <<= 1) {
		if (bit & supported_rate) {
			ret_rates |= bit;
			rates[*cnt] = iwl4965_rates[i].ieee |
				((bit & basic_rate) ? 0x80 : 0x00);
			(*cnt)++;
			(*left)--;
			if ((*left <= 0) ||
			    (*cnt >= IWL_SUPPORTED_RATES_IE_LEN))
				break;
		}
	}

	return ret_rates;
}

/**
 * iwl4965_fill_probe_req - fill in all required fields and IE for probe request
 */
static u16 iwl4965_fill_probe_req(struct iwl_priv *priv,
				  enum ieee80211_band band,
				  struct ieee80211_mgmt *frame,
				  int left, int is_direct)
{
	int len = 0;
	u8 *pos = NULL;
	u16 active_rates, ret_rates, cck_rates, active_rate_basic;
#ifdef CONFIG_IWL4965_HT
	const struct ieee80211_supported_band *sband =
						iwl4965_get_hw_mode(priv, band);
#endif /* CONFIG_IWL4965_HT */

	/* Make sure there is enough space for the probe request,
	 * two mandatory IEs and the data */
	left -= 24;
	if (left < 0)
		return 0;
	len += 24;

	frame->frame_control = cpu_to_le16(IEEE80211_STYPE_PROBE_REQ);
	memcpy(frame->da, iwl4965_broadcast_addr, ETH_ALEN);
	memcpy(frame->sa, priv->mac_addr, ETH_ALEN);
	memcpy(frame->bssid, iwl4965_broadcast_addr, ETH_ALEN);
	frame->seq_ctrl = 0;

	/* fill in our indirect SSID IE */
	/* ...next IE... */

	left -= 2;
	if (left < 0)
		return 0;
	len += 2;
	pos = &(frame->u.probe_req.variable[0]);
	*pos++ = WLAN_EID_SSID;
	*pos++ = 0;

	/* fill in our direct SSID IE... */
	if (is_direct) {
		/* ...next IE... */
		left -= 2 + priv->essid_len;
		if (left < 0)
			return 0;
		/* ... fill it in... */
		*pos++ = WLAN_EID_SSID;
		*pos++ = priv->essid_len;
		memcpy(pos, priv->essid, priv->essid_len);
		pos += priv->essid_len;
		len += 2 + priv->essid_len;
	}

	/* fill in supported rate */
	/* ...next IE... */
	left -= 2;
	if (left < 0)
		return 0;

	/* ... fill it in... */
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos = 0;

	/* exclude 60M rate */
	active_rates = priv->rates_mask;
	active_rates &= ~IWL_RATE_60M_MASK;

	active_rate_basic = active_rates & IWL_BASIC_RATES_MASK;

	cck_rates = IWL_CCK_RATES_MASK & active_rates;
	ret_rates = iwl4965_supported_rate_to_ie(pos, cck_rates,
			active_rate_basic, &left);
	active_rates &= ~ret_rates;

	ret_rates = iwl4965_supported_rate_to_ie(pos, active_rates,
				 active_rate_basic, &left);
	active_rates &= ~ret_rates;

	len += 2 + *pos;
	pos += (*pos) + 1;
	if (active_rates == 0)
		goto fill_end;

	/* fill in supported extended rate */
	/* ...next IE... */
	left -= 2;
	if (left < 0)
		return 0;
	/* ... fill it in... */
	*pos++ = WLAN_EID_EXT_SUPP_RATES;
	*pos = 0;
	iwl4965_supported_rate_to_ie(pos, active_rates,
				 active_rate_basic, &left);
	if (*pos > 0)
		len += 2 + *pos;

#ifdef CONFIG_IWL4965_HT
	if (sband && sband->ht_info.ht_supported) {
		struct ieee80211_ht_cap *ht_cap;
		pos += (*pos) + 1;
		*pos++ = WLAN_EID_HT_CAPABILITY;
		*pos++ = sizeof(struct ieee80211_ht_cap);
		ht_cap = (struct ieee80211_ht_cap *)pos;
		ht_cap->cap_info = cpu_to_le16(sband->ht_info.cap);
		memcpy(ht_cap->supp_mcs_set, sband->ht_info.supp_mcs_set, 16);
		ht_cap->ampdu_params_info =(sband->ht_info.ampdu_factor &
					    IEEE80211_HT_CAP_AMPDU_FACTOR) |
					    ((sband->ht_info.ampdu_density << 2) &
					    IEEE80211_HT_CAP_AMPDU_DENSITY);
		len += 2 + sizeof(struct ieee80211_ht_cap);
	}
#endif  /*CONFIG_IWL4965_HT */

 fill_end:
	return (u16)len;
}

/*
 * QoS  support
*/
static int iwl4965_send_qos_params_command(struct iwl_priv *priv,
				       struct iwl4965_qosparam_cmd *qos)
{

	return iwl_send_cmd_pdu(priv, REPLY_QOS_PARAM,
				sizeof(struct iwl4965_qosparam_cmd), qos);
}

static void iwl4965_activate_qos(struct iwl_priv *priv, u8 force)
{
	unsigned long flags;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (!priv->qos_data.qos_enable)
		return;

	spin_lock_irqsave(&priv->lock, flags);
	priv->qos_data.def_qos_parm.qos_flags = 0;

	if (priv->qos_data.qos_cap.q_AP.queue_request &&
	    !priv->qos_data.qos_cap.q_AP.txop_request)
		priv->qos_data.def_qos_parm.qos_flags |=
			QOS_PARAM_FLG_TXOP_TYPE_MSK;
	if (priv->qos_data.qos_active)
		priv->qos_data.def_qos_parm.qos_flags |=
			QOS_PARAM_FLG_UPDATE_EDCA_MSK;

#ifdef CONFIG_IWL4965_HT
	if (priv->current_ht_config.is_ht)
		priv->qos_data.def_qos_parm.qos_flags |= QOS_PARAM_FLG_TGN_MSK;
#endif /* CONFIG_IWL4965_HT */

	spin_unlock_irqrestore(&priv->lock, flags);

	if (force || iwl_is_associated(priv)) {
		IWL_DEBUG_QOS("send QoS cmd with Qos active=%d FLAGS=0x%X\n",
				priv->qos_data.qos_active,
				priv->qos_data.def_qos_parm.qos_flags);

		iwl4965_send_qos_params_command(priv,
				&(priv->qos_data.def_qos_parm));
	}
}

/*
 * Power management (not Tx power!) functions
 */
#define MSEC_TO_USEC 1024

#define NOSLP __constant_cpu_to_le16(0), 0, 0
#define SLP IWL_POWER_DRIVER_ALLOW_SLEEP_MSK, 0, 0
#define SLP_TIMEOUT(T) __constant_cpu_to_le32((T) * MSEC_TO_USEC)
#define SLP_VEC(X0, X1, X2, X3, X4) {__constant_cpu_to_le32(X0), \
				     __constant_cpu_to_le32(X1), \
				     __constant_cpu_to_le32(X2), \
				     __constant_cpu_to_le32(X3), \
				     __constant_cpu_to_le32(X4)}


/* default power management (not Tx power) table values */
/* for tim  0-10 */
static struct iwl4965_power_vec_entry range_0[IWL_POWER_AC] = {
	{{NOSLP, SLP_TIMEOUT(0), SLP_TIMEOUT(0), SLP_VEC(0, 0, 0, 0, 0)}, 0},
	{{SLP, SLP_TIMEOUT(200), SLP_TIMEOUT(500), SLP_VEC(1, 2, 3, 4, 4)}, 0},
	{{SLP, SLP_TIMEOUT(200), SLP_TIMEOUT(300), SLP_VEC(2, 4, 6, 7, 7)}, 0},
	{{SLP, SLP_TIMEOUT(50), SLP_TIMEOUT(100), SLP_VEC(2, 6, 9, 9, 10)}, 0},
	{{SLP, SLP_TIMEOUT(50), SLP_TIMEOUT(25), SLP_VEC(2, 7, 9, 9, 10)}, 1},
	{{SLP, SLP_TIMEOUT(25), SLP_TIMEOUT(25), SLP_VEC(4, 7, 10, 10, 10)}, 1}
};

/* for tim > 10 */
static struct iwl4965_power_vec_entry range_1[IWL_POWER_AC] = {
	{{NOSLP, SLP_TIMEOUT(0), SLP_TIMEOUT(0), SLP_VEC(0, 0, 0, 0, 0)}, 0},
	{{SLP, SLP_TIMEOUT(200), SLP_TIMEOUT(500),
		 SLP_VEC(1, 2, 3, 4, 0xFF)}, 0},
	{{SLP, SLP_TIMEOUT(200), SLP_TIMEOUT(300),
		 SLP_VEC(2, 4, 6, 7, 0xFF)}, 0},
	{{SLP, SLP_TIMEOUT(50), SLP_TIMEOUT(100),
		 SLP_VEC(2, 6, 9, 9, 0xFF)}, 0},
	{{SLP, SLP_TIMEOUT(50), SLP_TIMEOUT(25), SLP_VEC(2, 7, 9, 9, 0xFF)}, 0},
	{{SLP, SLP_TIMEOUT(25), SLP_TIMEOUT(25),
		 SLP_VEC(4, 7, 10, 10, 0xFF)}, 0}
};

int iwl4965_power_init_handle(struct iwl_priv *priv)
{
	int rc = 0, i;
	struct iwl4965_power_mgr *pow_data;
	int size = sizeof(struct iwl4965_power_vec_entry) * IWL_POWER_AC;
	u16 pci_pm;

	IWL_DEBUG_POWER("Initialize power \n");

	pow_data = &(priv->power_data);

	memset(pow_data, 0, sizeof(*pow_data));

	pow_data->active_index = IWL_POWER_RANGE_0;
	pow_data->dtim_val = 0xffff;

	memcpy(&pow_data->pwr_range_0[0], &range_0[0], size);
	memcpy(&pow_data->pwr_range_1[0], &range_1[0], size);

	rc = pci_read_config_word(priv->pci_dev, PCI_LINK_CTRL, &pci_pm);
	if (rc != 0)
		return 0;
	else {
		struct iwl4965_powertable_cmd *cmd;

		IWL_DEBUG_POWER("adjust power command flags\n");

		for (i = 0; i < IWL_POWER_AC; i++) {
			cmd = &pow_data->pwr_range_0[i].cmd;

			if (pci_pm & 0x1)
				cmd->flags &= ~IWL_POWER_PCI_PM_MSK;
			else
				cmd->flags |= IWL_POWER_PCI_PM_MSK;
		}
	}
	return rc;
}

static int iwl4965_update_power_cmd(struct iwl_priv *priv,
				struct iwl4965_powertable_cmd *cmd, u32 mode)
{
	int rc = 0, i;
	u8 skip;
	u32 max_sleep = 0;
	struct iwl4965_power_vec_entry *range;
	u8 period = 0;
	struct iwl4965_power_mgr *pow_data;

	if (mode > IWL_POWER_INDEX_5) {
		IWL_DEBUG_POWER("Error invalid power mode \n");
		return -1;
	}
	pow_data = &(priv->power_data);

	if (pow_data->active_index == IWL_POWER_RANGE_0)
		range = &pow_data->pwr_range_0[0];
	else
		range = &pow_data->pwr_range_1[1];

	memcpy(cmd, &range[mode].cmd, sizeof(struct iwl4965_powertable_cmd));

#ifdef IWL_MAC80211_DISABLE
	if (priv->assoc_network != NULL) {
		unsigned long flags;

		period = priv->assoc_network->tim.tim_period;
	}
#endif	/*IWL_MAC80211_DISABLE */
	skip = range[mode].no_dtim;

	if (period == 0) {
		period = 1;
		skip = 0;
	}

	if (skip == 0) {
		max_sleep = period;
		cmd->flags &= ~IWL_POWER_SLEEP_OVER_DTIM_MSK;
	} else {
		__le32 slp_itrvl = cmd->sleep_interval[IWL_POWER_VEC_SIZE - 1];
		max_sleep = (le32_to_cpu(slp_itrvl) / period) * period;
		cmd->flags |= IWL_POWER_SLEEP_OVER_DTIM_MSK;
	}

	for (i = 0; i < IWL_POWER_VEC_SIZE; i++) {
		if (le32_to_cpu(cmd->sleep_interval[i]) > max_sleep)
			cmd->sleep_interval[i] = cpu_to_le32(max_sleep);
	}

	IWL_DEBUG_POWER("Flags value = 0x%08X\n", cmd->flags);
	IWL_DEBUG_POWER("Tx timeout = %u\n", le32_to_cpu(cmd->tx_data_timeout));
	IWL_DEBUG_POWER("Rx timeout = %u\n", le32_to_cpu(cmd->rx_data_timeout));
	IWL_DEBUG_POWER("Sleep interval vector = { %d , %d , %d , %d , %d }\n",
			le32_to_cpu(cmd->sleep_interval[0]),
			le32_to_cpu(cmd->sleep_interval[1]),
			le32_to_cpu(cmd->sleep_interval[2]),
			le32_to_cpu(cmd->sleep_interval[3]),
			le32_to_cpu(cmd->sleep_interval[4]));

	return rc;
}

static int iwl4965_send_power_mode(struct iwl_priv *priv, u32 mode)
{
	u32 uninitialized_var(final_mode);
	int rc;
	struct iwl4965_powertable_cmd cmd;

	/* If on battery, set to 3,
	 * if plugged into AC power, set to CAM ("continuously aware mode"),
	 * else user level */
	switch (mode) {
	case IWL_POWER_BATTERY:
		final_mode = IWL_POWER_INDEX_3;
		break;
	case IWL_POWER_AC:
		final_mode = IWL_POWER_MODE_CAM;
		break;
	default:
		final_mode = mode;
		break;
	}

	cmd.keep_alive_beacons = 0;

	iwl4965_update_power_cmd(priv, &cmd, final_mode);

	rc = iwl_send_cmd_pdu(priv, POWER_TABLE_CMD, sizeof(cmd), &cmd);

	if (final_mode == IWL_POWER_MODE_CAM)
		clear_bit(STATUS_POWER_PMI, &priv->status);
	else
		set_bit(STATUS_POWER_PMI, &priv->status);

	return rc;
}

int iwl4965_is_network_packet(struct iwl_priv *priv, struct ieee80211_hdr *header)
{
	/* Filter incoming packets to determine if they are targeted toward
	 * this network, discarding packets coming from ourselves */
	switch (priv->iw_mode) {
	case IEEE80211_IF_TYPE_IBSS: /* Header: Dest. | Source    | BSSID */
		/* packets from our adapter are dropped (echo) */
		if (!compare_ether_addr(header->addr2, priv->mac_addr))
			return 0;
		/* {broad,multi}cast packets to our IBSS go through */
		if (is_multicast_ether_addr(header->addr1))
			return !compare_ether_addr(header->addr3, priv->bssid);
		/* packets to our adapter go through */
		return !compare_ether_addr(header->addr1, priv->mac_addr);
	case IEEE80211_IF_TYPE_STA: /* Header: Dest. | AP{BSSID} | Source */
		/* packets from our adapter are dropped (echo) */
		if (!compare_ether_addr(header->addr3, priv->mac_addr))
			return 0;
		/* {broad,multi}cast packets to our BSS go through */
		if (is_multicast_ether_addr(header->addr1))
			return !compare_ether_addr(header->addr2, priv->bssid);
		/* packets to our adapter go through */
		return !compare_ether_addr(header->addr1, priv->mac_addr);
	default:
		break;
	}

	return 1;
}

#define TX_STATUS_ENTRY(x) case TX_STATUS_FAIL_ ## x: return #x

static const char *iwl4965_get_tx_fail_reason(u32 status)
{
	switch (status & TX_STATUS_MSK) {
	case TX_STATUS_SUCCESS:
		return "SUCCESS";
		TX_STATUS_ENTRY(SHORT_LIMIT);
		TX_STATUS_ENTRY(LONG_LIMIT);
		TX_STATUS_ENTRY(FIFO_UNDERRUN);
		TX_STATUS_ENTRY(MGMNT_ABORT);
		TX_STATUS_ENTRY(NEXT_FRAG);
		TX_STATUS_ENTRY(LIFE_EXPIRE);
		TX_STATUS_ENTRY(DEST_PS);
		TX_STATUS_ENTRY(ABORTED);
		TX_STATUS_ENTRY(BT_RETRY);
		TX_STATUS_ENTRY(STA_INVALID);
		TX_STATUS_ENTRY(FRAG_DROPPED);
		TX_STATUS_ENTRY(TID_DISABLE);
		TX_STATUS_ENTRY(FRAME_FLUSHED);
		TX_STATUS_ENTRY(INSUFFICIENT_CF_POLL);
		TX_STATUS_ENTRY(TX_LOCKED);
		TX_STATUS_ENTRY(NO_BEACON_ON_RADAR);
	}

	return "UNKNOWN";
}

/**
 * iwl4965_scan_cancel - Cancel any currently executing HW scan
 *
 * NOTE: priv->mutex is not required before calling this function
 */
static int iwl4965_scan_cancel(struct iwl_priv *priv)
{
	if (!test_bit(STATUS_SCAN_HW, &priv->status)) {
		clear_bit(STATUS_SCANNING, &priv->status);
		return 0;
	}

	if (test_bit(STATUS_SCANNING, &priv->status)) {
		if (!test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
			IWL_DEBUG_SCAN("Queuing scan abort.\n");
			set_bit(STATUS_SCAN_ABORTING, &priv->status);
			queue_work(priv->workqueue, &priv->abort_scan);

		} else
			IWL_DEBUG_SCAN("Scan abort already in progress.\n");

		return test_bit(STATUS_SCANNING, &priv->status);
	}

	return 0;
}

/**
 * iwl4965_scan_cancel_timeout - Cancel any currently executing HW scan
 * @ms: amount of time to wait (in milliseconds) for scan to abort
 *
 * NOTE: priv->mutex must be held before calling this function
 */
static int iwl4965_scan_cancel_timeout(struct iwl_priv *priv, unsigned long ms)
{
	unsigned long now = jiffies;
	int ret;

	ret = iwl4965_scan_cancel(priv);
	if (ret && ms) {
		mutex_unlock(&priv->mutex);
		while (!time_after(jiffies, now + msecs_to_jiffies(ms)) &&
				test_bit(STATUS_SCANNING, &priv->status))
			msleep(1);
		mutex_lock(&priv->mutex);

		return test_bit(STATUS_SCANNING, &priv->status);
	}

	return ret;
}

static void iwl4965_sequence_reset(struct iwl_priv *priv)
{
	/* Reset ieee stats */

	/* We don't reset the net_device_stats (ieee->stats) on
	 * re-association */

	priv->last_seq_num = -1;
	priv->last_frag_num = -1;
	priv->last_packet_time = 0;

	iwl4965_scan_cancel(priv);
}

#define MAX_UCODE_BEACON_INTERVAL	4096
#define INTEL_CONN_LISTEN_INTERVAL	__constant_cpu_to_le16(0xA)

static __le16 iwl4965_adjust_beacon_interval(u16 beacon_val)
{
	u16 new_val = 0;
	u16 beacon_factor = 0;

	beacon_factor =
	    (beacon_val + MAX_UCODE_BEACON_INTERVAL)
		/ MAX_UCODE_BEACON_INTERVAL;
	new_val = beacon_val / beacon_factor;

	return cpu_to_le16(new_val);
}

static void iwl4965_setup_rxon_timing(struct iwl_priv *priv)
{
	u64 interval_tm_unit;
	u64 tsf, result;
	unsigned long flags;
	struct ieee80211_conf *conf = NULL;
	u16 beacon_int = 0;

	conf = ieee80211_get_hw_conf(priv->hw);

	spin_lock_irqsave(&priv->lock, flags);
	priv->rxon_timing.timestamp.dw[1] = cpu_to_le32(priv->timestamp >> 32);
	priv->rxon_timing.timestamp.dw[0] =
				cpu_to_le32(priv->timestamp & 0xFFFFFFFF);

	priv->rxon_timing.listen_interval = INTEL_CONN_LISTEN_INTERVAL;

	tsf = priv->timestamp;

	beacon_int = priv->beacon_int;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (priv->iw_mode == IEEE80211_IF_TYPE_STA) {
		if (beacon_int == 0) {
			priv->rxon_timing.beacon_interval = cpu_to_le16(100);
			priv->rxon_timing.beacon_init_val = cpu_to_le32(102400);
		} else {
			priv->rxon_timing.beacon_interval =
				cpu_to_le16(beacon_int);
			priv->rxon_timing.beacon_interval =
			    iwl4965_adjust_beacon_interval(
				le16_to_cpu(priv->rxon_timing.beacon_interval));
		}

		priv->rxon_timing.atim_window = 0;
	} else {
		priv->rxon_timing.beacon_interval =
			iwl4965_adjust_beacon_interval(conf->beacon_int);
		/* TODO: we need to get atim_window from upper stack
		 * for now we set to 0 */
		priv->rxon_timing.atim_window = 0;
	}

	interval_tm_unit =
		(le16_to_cpu(priv->rxon_timing.beacon_interval) * 1024);
	result = do_div(tsf, interval_tm_unit);
	priv->rxon_timing.beacon_init_val =
	    cpu_to_le32((u32) ((u64) interval_tm_unit - result));

	IWL_DEBUG_ASSOC
	    ("beacon interval %d beacon timer %d beacon tim %d\n",
		le16_to_cpu(priv->rxon_timing.beacon_interval),
		le32_to_cpu(priv->rxon_timing.beacon_init_val),
		le16_to_cpu(priv->rxon_timing.atim_window));
}

static int iwl4965_scan_initiate(struct iwl_priv *priv)
{
	if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {
		IWL_ERROR("APs don't scan.\n");
		return 0;
	}

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_SCAN("Aborting scan due to not ready.\n");
		return -EIO;
	}

	if (test_bit(STATUS_SCANNING, &priv->status)) {
		IWL_DEBUG_SCAN("Scan already in progress.\n");
		return -EAGAIN;
	}

	if (test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_SCAN("Scan request while abort pending.  "
			       "Queuing.\n");
		return -EAGAIN;
	}

	IWL_DEBUG_INFO("Starting scan...\n");
	priv->scan_bands = 2;
	set_bit(STATUS_SCANNING, &priv->status);
	priv->scan_start = jiffies;
	priv->scan_pass_start = priv->scan_start;

	queue_work(priv->workqueue, &priv->request_scan);

	return 0;
}


static void iwl4965_set_flags_for_phymode(struct iwl_priv *priv,
					  enum ieee80211_band band)
{
	if (band == IEEE80211_BAND_5GHZ) {
		priv->staging_rxon.flags &=
		    ~(RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK
		      | RXON_FLG_CCK_MSK);
		priv->staging_rxon.flags |= RXON_FLG_SHORT_SLOT_MSK;
	} else {
		/* Copied from iwl4965_post_associate() */
		if (priv->assoc_capability & WLAN_CAPABILITY_SHORT_SLOT_TIME)
			priv->staging_rxon.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS)
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		priv->staging_rxon.flags |= RXON_FLG_BAND_24G_MSK;
		priv->staging_rxon.flags |= RXON_FLG_AUTO_DETECT_MSK;
		priv->staging_rxon.flags &= ~RXON_FLG_CCK_MSK;
	}
}

/*
 * initialize rxon structure with default values from eeprom
 */
static void iwl4965_connection_init_rx_config(struct iwl_priv *priv)
{
	const struct iwl_channel_info *ch_info;

	memset(&priv->staging_rxon, 0, sizeof(priv->staging_rxon));

	switch (priv->iw_mode) {
	case IEEE80211_IF_TYPE_AP:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_AP;
		break;

	case IEEE80211_IF_TYPE_STA:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_ESS;
		priv->staging_rxon.filter_flags = RXON_FILTER_ACCEPT_GRP_MSK;
		break;

	case IEEE80211_IF_TYPE_IBSS:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_IBSS;
		priv->staging_rxon.flags = RXON_FLG_SHORT_PREAMBLE_MSK;
		priv->staging_rxon.filter_flags = RXON_FILTER_BCON_AWARE_MSK |
						  RXON_FILTER_ACCEPT_GRP_MSK;
		break;

	case IEEE80211_IF_TYPE_MNTR:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_SNIFFER;
		priv->staging_rxon.filter_flags = RXON_FILTER_PROMISC_MSK |
		    RXON_FILTER_CTL2HOST_MSK | RXON_FILTER_ACCEPT_GRP_MSK;
		break;
	default:
		IWL_ERROR("Unsupported interface type %d\n", priv->iw_mode);
		break;
	}

#if 0
	/* TODO:  Figure out when short_preamble would be set and cache from
	 * that */
	if (!hw_to_local(priv->hw)->short_preamble)
		priv->staging_rxon.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		priv->staging_rxon.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
#endif

	ch_info = iwl_get_channel_info(priv, priv->band,
				       le16_to_cpu(priv->staging_rxon.channel));

	if (!ch_info)
		ch_info = &priv->channel_info[0];

	/*
	 * in some case A channels are all non IBSS
	 * in this case force B/G channel
	 */
	if ((priv->iw_mode == IEEE80211_IF_TYPE_IBSS) &&
	    !(is_channel_ibss(ch_info)))
		ch_info = &priv->channel_info[0];

	priv->staging_rxon.channel = cpu_to_le16(ch_info->channel);
	priv->band = ch_info->band;

	iwl4965_set_flags_for_phymode(priv, priv->band);

	priv->staging_rxon.ofdm_basic_rates =
	    (IWL_OFDM_RATES_MASK >> IWL_FIRST_OFDM_RATE) & 0xFF;
	priv->staging_rxon.cck_basic_rates =
	    (IWL_CCK_RATES_MASK >> IWL_FIRST_CCK_RATE) & 0xF;

	priv->staging_rxon.flags &= ~(RXON_FLG_CHANNEL_MODE_MIXED_MSK |
					RXON_FLG_CHANNEL_MODE_PURE_40_MSK);
	memcpy(priv->staging_rxon.node_addr, priv->mac_addr, ETH_ALEN);
	memcpy(priv->staging_rxon.wlap_bssid_addr, priv->mac_addr, ETH_ALEN);
	priv->staging_rxon.ofdm_ht_single_stream_basic_rates = 0xff;
	priv->staging_rxon.ofdm_ht_dual_stream_basic_rates = 0xff;
	iwl4965_set_rxon_chain(priv);
}

static int iwl4965_set_mode(struct iwl_priv *priv, int mode)
{
	if (mode == IEEE80211_IF_TYPE_IBSS) {
		const struct iwl_channel_info *ch_info;

		ch_info = iwl_get_channel_info(priv,
			priv->band,
			le16_to_cpu(priv->staging_rxon.channel));

		if (!ch_info || !is_channel_ibss(ch_info)) {
			IWL_ERROR("channel %d not IBSS channel\n",
				  le16_to_cpu(priv->staging_rxon.channel));
			return -EINVAL;
		}
	}

	priv->iw_mode = mode;

	iwl4965_connection_init_rx_config(priv);
	memcpy(priv->staging_rxon.node_addr, priv->mac_addr, ETH_ALEN);

	iwlcore_clear_stations_table(priv);

	/* dont commit rxon if rf-kill is on*/
	if (!iwl_is_ready_rf(priv))
		return -EAGAIN;

	cancel_delayed_work(&priv->scan_check);
	if (iwl4965_scan_cancel_timeout(priv, 100)) {
		IWL_WARNING("Aborted scan still in progress after 100ms\n");
		IWL_DEBUG_MAC80211("leaving - scan abort failed.\n");
		return -EAGAIN;
	}

	iwl4965_commit_rxon(priv);

	return 0;
}

static void iwl4965_build_tx_cmd_hwcrypto(struct iwl_priv *priv,
				      struct ieee80211_tx_control *ctl,
				      struct iwl_cmd *cmd,
				      struct sk_buff *skb_frag,
				      int sta_id)
{
	struct iwl4965_hw_key *keyinfo = &priv->stations[sta_id].keyinfo;
	struct iwl_wep_key *wepkey;
	int keyidx = 0;

	BUG_ON(ctl->key_idx > 3);

	switch (keyinfo->alg) {
	case ALG_CCMP:
		cmd->cmd.tx.sec_ctl = TX_CMD_SEC_CCM;
		memcpy(cmd->cmd.tx.key, keyinfo->key, keyinfo->keylen);
		if (ctl->flags & IEEE80211_TXCTL_AMPDU)
			cmd->cmd.tx.tx_flags |= TX_CMD_FLG_AGG_CCMP_MSK;
		IWL_DEBUG_TX("tx_cmd with aes hwcrypto\n");
		break;

	case ALG_TKIP:
		cmd->cmd.tx.sec_ctl = TX_CMD_SEC_TKIP;
		ieee80211_get_tkip_key(keyinfo->conf, skb_frag,
			IEEE80211_TKIP_P2_KEY, cmd->cmd.tx.key);
		IWL_DEBUG_TX("tx_cmd with tkip hwcrypto\n");
		break;

	case ALG_WEP:
		wepkey = &priv->wep_keys[ctl->key_idx];
		cmd->cmd.tx.sec_ctl = 0;
		if (priv->default_wep_key) {
			/* the WEP key was sent as static */
			keyidx = ctl->key_idx;
			memcpy(&cmd->cmd.tx.key[3], wepkey->key,
							wepkey->key_size);
			if (wepkey->key_size == WEP_KEY_LEN_128)
				cmd->cmd.tx.sec_ctl |= TX_CMD_SEC_KEY128;
		} else {
			/* the WEP key was sent as dynamic */
			keyidx = keyinfo->keyidx;
			memcpy(&cmd->cmd.tx.key[3], keyinfo->key,
							keyinfo->keylen);
			if (keyinfo->keylen == WEP_KEY_LEN_128)
				cmd->cmd.tx.sec_ctl |= TX_CMD_SEC_KEY128;
		}

		cmd->cmd.tx.sec_ctl |= (TX_CMD_SEC_WEP |
			(keyidx & TX_CMD_SEC_MSK) << TX_CMD_SEC_SHIFT);

		IWL_DEBUG_TX("Configuring packet for WEP encryption "
			     "with key %d\n", keyidx);
		break;

	default:
		printk(KERN_ERR "Unknown encode alg %d\n", keyinfo->alg);
		break;
	}
}

/*
 * handle build REPLY_TX command notification.
 */
static void iwl4965_build_tx_cmd_basic(struct iwl_priv *priv,
				  struct iwl_cmd *cmd,
				  struct ieee80211_tx_control *ctrl,
				  struct ieee80211_hdr *hdr,
				  int is_unicast, u8 std_id)
{
	__le16 *qc;
	u16 fc = le16_to_cpu(hdr->frame_control);
	__le32 tx_flags = cmd->cmd.tx.tx_flags;

	cmd->cmd.tx.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;
	if (!(ctrl->flags & IEEE80211_TXCTL_NO_ACK)) {
		tx_flags |= TX_CMD_FLG_ACK_MSK;
		if ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT)
			tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
		if (ieee80211_is_probe_response(fc) &&
		    !(le16_to_cpu(hdr->seq_ctrl) & 0xf))
			tx_flags |= TX_CMD_FLG_TSF_MSK;
	} else {
		tx_flags &= (~TX_CMD_FLG_ACK_MSK);
		tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
	}

	if (ieee80211_is_back_request(fc))
		tx_flags |= TX_CMD_FLG_ACK_MSK | TX_CMD_FLG_IMM_BA_RSP_MASK;


	cmd->cmd.tx.sta_id = std_id;
	if (ieee80211_get_morefrag(hdr))
		tx_flags |= TX_CMD_FLG_MORE_FRAG_MSK;

	qc = ieee80211_get_qos_ctrl(hdr);
	if (qc) {
		cmd->cmd.tx.tid_tspec = (u8) (le16_to_cpu(*qc) & 0xf);
		tx_flags &= ~TX_CMD_FLG_SEQ_CTL_MSK;
	} else
		tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;

	if (ctrl->flags & IEEE80211_TXCTL_USE_RTS_CTS) {
		tx_flags |= TX_CMD_FLG_RTS_MSK;
		tx_flags &= ~TX_CMD_FLG_CTS_MSK;
	} else if (ctrl->flags & IEEE80211_TXCTL_USE_CTS_PROTECT) {
		tx_flags &= ~TX_CMD_FLG_RTS_MSK;
		tx_flags |= TX_CMD_FLG_CTS_MSK;
	}

	if ((tx_flags & TX_CMD_FLG_RTS_MSK) || (tx_flags & TX_CMD_FLG_CTS_MSK))
		tx_flags |= TX_CMD_FLG_FULL_TXOP_PROT_MSK;

	tx_flags &= ~(TX_CMD_FLG_ANT_SEL_MSK);
	if ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) {
		if ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_ASSOC_REQ ||
		    (fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_REASSOC_REQ)
			cmd->cmd.tx.timeout.pm_frame_timeout = cpu_to_le16(3);
		else
			cmd->cmd.tx.timeout.pm_frame_timeout = cpu_to_le16(2);
	} else {
		cmd->cmd.tx.timeout.pm_frame_timeout = 0;
	}

	cmd->cmd.tx.driver_txop = 0;
	cmd->cmd.tx.tx_flags = tx_flags;
	cmd->cmd.tx.next_frame_len = 0;
}
static void iwl_update_tx_stats(struct iwl_priv *priv, u16 fc, u16 len)
{
	/* 0 - mgmt, 1 - cnt, 2 - data */
	int idx = (fc & IEEE80211_FCTL_FTYPE) >> 2;
	priv->tx_stats[idx].cnt++;
	priv->tx_stats[idx].bytes += len;
}
/**
 * iwl4965_get_sta_id - Find station's index within station table
 *
 * If new IBSS station, create new entry in station table
 */
static int iwl4965_get_sta_id(struct iwl_priv *priv,
				struct ieee80211_hdr *hdr)
{
	int sta_id;
	u16 fc = le16_to_cpu(hdr->frame_control);
	DECLARE_MAC_BUF(mac);

	/* If this frame is broadcast or management, use broadcast station id */
	if (((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA) ||
	    is_multicast_ether_addr(hdr->addr1))
		return priv->hw_params.bcast_sta_id;

	switch (priv->iw_mode) {

	/* If we are a client station in a BSS network, use the special
	 * AP station entry (that's the only station we communicate with) */
	case IEEE80211_IF_TYPE_STA:
		return IWL_AP_ID;

	/* If we are an AP, then find the station, or use BCAST */
	case IEEE80211_IF_TYPE_AP:
		sta_id = iwl4965_hw_find_station(priv, hdr->addr1);
		if (sta_id != IWL_INVALID_STATION)
			return sta_id;
		return priv->hw_params.bcast_sta_id;

	/* If this frame is going out to an IBSS network, find the station,
	 * or create a new station table entry */
	case IEEE80211_IF_TYPE_IBSS:
		sta_id = iwl4965_hw_find_station(priv, hdr->addr1);
		if (sta_id != IWL_INVALID_STATION)
			return sta_id;

		/* Create new station table entry */
		sta_id = iwl4965_add_station_flags(priv, hdr->addr1,
						   0, CMD_ASYNC, NULL);

		if (sta_id != IWL_INVALID_STATION)
			return sta_id;

		IWL_DEBUG_DROP("Station %s not in station map. "
			       "Defaulting to broadcast...\n",
			       print_mac(mac, hdr->addr1));
		iwl_print_hex_dump(IWL_DL_DROP, (u8 *) hdr, sizeof(*hdr));
		return priv->hw_params.bcast_sta_id;

	default:
		IWL_WARNING("Unknown mode of operation: %d", priv->iw_mode);
		return priv->hw_params.bcast_sta_id;
	}
}

/*
 * start REPLY_TX command process
 */
static int iwl4965_tx_skb(struct iwl_priv *priv,
		      struct sk_buff *skb, struct ieee80211_tx_control *ctl)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct iwl4965_tfd_frame *tfd;
	u32 *control_flags;
	int txq_id = ctl->queue;
	struct iwl4965_tx_queue *txq = NULL;
	struct iwl4965_queue *q = NULL;
	dma_addr_t phys_addr;
	dma_addr_t txcmd_phys;
	dma_addr_t scratch_phys;
	struct iwl_cmd *out_cmd = NULL;
	u16 len, idx, len_org;
	u8 id, hdr_len, unicast;
	u8 sta_id;
	u16 seq_number = 0;
	u16 fc;
	__le16 *qc;
	u8 wait_write_ptr = 0;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&priv->lock, flags);
	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_DROP("Dropping - RF KILL\n");
		goto drop_unlock;
	}

	if (!priv->vif) {
		IWL_DEBUG_DROP("Dropping - !priv->vif\n");
		goto drop_unlock;
	}

	if ((ctl->tx_rate->hw_value & 0xFF) == IWL_INVALID_RATE) {
		IWL_ERROR("ERROR: No TX rate available.\n");
		goto drop_unlock;
	}

	unicast = !is_multicast_ether_addr(hdr->addr1);
	id = 0;

	fc = le16_to_cpu(hdr->frame_control);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (ieee80211_is_auth(fc))
		IWL_DEBUG_TX("Sending AUTH frame\n");
	else if (ieee80211_is_assoc_request(fc))
		IWL_DEBUG_TX("Sending ASSOC frame\n");
	else if (ieee80211_is_reassoc_request(fc))
		IWL_DEBUG_TX("Sending REASSOC frame\n");
#endif

	/* drop all data frame if we are not associated */
	if (((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) &&
	   (!iwl_is_associated(priv) ||
	    ((priv->iw_mode == IEEE80211_IF_TYPE_STA) && !priv->assoc_id) ||
	    !priv->assoc_station_added)) {
		IWL_DEBUG_DROP("Dropping - !iwl_is_associated\n");
		goto drop_unlock;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	hdr_len = ieee80211_get_hdrlen(fc);

	/* Find (or create) index into station table for destination station */
	sta_id = iwl4965_get_sta_id(priv, hdr);
	if (sta_id == IWL_INVALID_STATION) {
		DECLARE_MAC_BUF(mac);

		IWL_DEBUG_DROP("Dropping - INVALID STATION: %s\n",
			       print_mac(mac, hdr->addr1));
		goto drop;
	}

	IWL_DEBUG_RATE("station Id %d\n", sta_id);

	qc = ieee80211_get_qos_ctrl(hdr);
	if (qc) {
		u8 tid = (u8)(le16_to_cpu(*qc) & 0xf);
		seq_number = priv->stations[sta_id].tid[tid].seq_number &
				IEEE80211_SCTL_SEQ;
		hdr->seq_ctrl = cpu_to_le16(seq_number) |
			(hdr->seq_ctrl &
				__constant_cpu_to_le16(IEEE80211_SCTL_FRAG));
		seq_number += 0x10;
#ifdef CONFIG_IWL4965_HT
		/* aggregation is on for this <sta,tid> */
		if (ctl->flags & IEEE80211_TXCTL_AMPDU)
			txq_id = priv->stations[sta_id].tid[tid].agg.txq_id;
		priv->stations[sta_id].tid[tid].tfds_in_queue++;
#endif /* CONFIG_IWL4965_HT */
	}

	/* Descriptor for chosen Tx queue */
	txq = &priv->txq[txq_id];
	q = &txq->q;

	spin_lock_irqsave(&priv->lock, flags);

	/* Set up first empty TFD within this queue's circular TFD buffer */
	tfd = &txq->bd[q->write_ptr];
	memset(tfd, 0, sizeof(*tfd));
	control_flags = (u32 *) tfd;
	idx = get_cmd_index(q, q->write_ptr, 0);

	/* Set up driver data for this TFD */
	memset(&(txq->txb[q->write_ptr]), 0, sizeof(struct iwl4965_tx_info));
	txq->txb[q->write_ptr].skb[0] = skb;
	memcpy(&(txq->txb[q->write_ptr].status.control),
	       ctl, sizeof(struct ieee80211_tx_control));

	/* Set up first empty entry in queue's array of Tx/cmd buffers */
	out_cmd = &txq->cmd[idx];
	memset(&out_cmd->hdr, 0, sizeof(out_cmd->hdr));
	memset(&out_cmd->cmd.tx, 0, sizeof(out_cmd->cmd.tx));

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
	memcpy(out_cmd->cmd.tx.hdr, hdr, hdr_len);

	/*
	 * Use the first empty entry in this queue's command buffer array
	 * to contain the Tx command and MAC header concatenated together
	 * (payload data will be in another buffer).
	 * Size of this varies, due to varying MAC header length.
	 * If end is not dword aligned, we'll have 2 extra bytes at the end
	 * of the MAC header (device reads on dword boundaries).
	 * We'll tell device about this padding later.
	 */
	len = priv->hw_params.tx_cmd_len +
		sizeof(struct iwl_cmd_header) + hdr_len;

	len_org = len;
	len = (len + 3) & ~3;

	if (len_org != len)
		len_org = 1;
	else
		len_org = 0;

	/* Physical address of this Tx command's header (not MAC header!),
	 * within command buffer array. */
	txcmd_phys = txq->dma_addr_cmd + sizeof(struct iwl_cmd) * idx +
		     offsetof(struct iwl_cmd, hdr);

	/* Add buffer containing Tx command and MAC(!) header to TFD's
	 * first entry */
	iwl4965_hw_txq_attach_buf_to_tfd(priv, tfd, txcmd_phys, len);

	if (!(ctl->flags & IEEE80211_TXCTL_DO_NOT_ENCRYPT))
		iwl4965_build_tx_cmd_hwcrypto(priv, ctl, out_cmd, skb, sta_id);

	/* Set up TFD's 2nd entry to point directly to remainder of skb,
	 * if any (802.11 null frames have no payload). */
	len = skb->len - hdr_len;
	if (len) {
		phys_addr = pci_map_single(priv->pci_dev, skb->data + hdr_len,
					   len, PCI_DMA_TODEVICE);
		iwl4965_hw_txq_attach_buf_to_tfd(priv, tfd, phys_addr, len);
	}

	/* Tell 4965 about any 2-byte padding after MAC header */
	if (len_org)
		out_cmd->cmd.tx.tx_flags |= TX_CMD_FLG_MH_PAD_MSK;

	/* Total # bytes to be transmitted */
	len = (u16)skb->len;
	out_cmd->cmd.tx.len = cpu_to_le16(len);

	/* TODO need this for burst mode later on */
	iwl4965_build_tx_cmd_basic(priv, out_cmd, ctl, hdr, unicast, sta_id);

	/* set is_hcca to 0; it probably will never be implemented */
	iwl4965_hw_build_tx_cmd_rate(priv, out_cmd, ctl, hdr, sta_id, 0);

	iwl_update_tx_stats(priv, fc, len);

	scratch_phys = txcmd_phys + sizeof(struct iwl_cmd_header) +
		offsetof(struct iwl4965_tx_cmd, scratch);
	out_cmd->cmd.tx.dram_lsb_ptr = cpu_to_le32(scratch_phys);
	out_cmd->cmd.tx.dram_msb_ptr = iwl_get_dma_hi_address(scratch_phys);

	if (!ieee80211_get_morefrag(hdr)) {
		txq->need_update = 1;
		if (qc) {
			u8 tid = (u8)(le16_to_cpu(*qc) & 0xf);
			priv->stations[sta_id].tid[tid].seq_number = seq_number;
		}
	} else {
		wait_write_ptr = 1;
		txq->need_update = 0;
	}

	iwl_print_hex_dump(IWL_DL_TX, out_cmd->cmd.payload,
			   sizeof(out_cmd->cmd.tx));

	iwl_print_hex_dump(IWL_DL_TX, (u8 *)out_cmd->cmd.tx.hdr,
			   ieee80211_get_hdrlen(fc));

	/* Set up entry for this TFD in Tx byte-count array */
	priv->cfg->ops->lib->txq_update_byte_cnt_tbl(priv, txq, len);

	/* Tell device the write index *just past* this latest filled TFD */
	q->write_ptr = iwl_queue_inc_wrap(q->write_ptr, q->n_bd);
	rc = iwl4965_tx_queue_update_write_ptr(priv, txq);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (rc)
		return rc;

	if ((iwl4965_queue_space(q) < q->high_mark)
	    && priv->mac80211_registered) {
		if (wait_write_ptr) {
			spin_lock_irqsave(&priv->lock, flags);
			txq->need_update = 1;
			iwl4965_tx_queue_update_write_ptr(priv, txq);
			spin_unlock_irqrestore(&priv->lock, flags);
		}

		ieee80211_stop_queue(priv->hw, ctl->queue);
	}

	return 0;

drop_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
drop:
	return -1;
}

static void iwl4965_set_rate(struct iwl_priv *priv)
{
	const struct ieee80211_supported_band *hw = NULL;
	struct ieee80211_rate *rate;
	int i;

	hw = iwl4965_get_hw_mode(priv, priv->band);
	if (!hw) {
		IWL_ERROR("Failed to set rate: unable to get hw mode\n");
		return;
	}

	priv->active_rate = 0;
	priv->active_rate_basic = 0;

	for (i = 0; i < hw->n_bitrates; i++) {
		rate = &(hw->bitrates[i]);
		if (rate->hw_value < IWL_RATE_COUNT)
			priv->active_rate |= (1 << rate->hw_value);
	}

	IWL_DEBUG_RATE("Set active_rate = %0x, active_rate_basic = %0x\n",
		       priv->active_rate, priv->active_rate_basic);

	/*
	 * If a basic rate is configured, then use it (adding IWL_RATE_1M_MASK)
	 * otherwise set it to the default of all CCK rates and 6, 12, 24 for
	 * OFDM
	 */
	if (priv->active_rate_basic & IWL_CCK_BASIC_RATES_MASK)
		priv->staging_rxon.cck_basic_rates =
		    ((priv->active_rate_basic &
		      IWL_CCK_RATES_MASK) >> IWL_FIRST_CCK_RATE) & 0xF;
	else
		priv->staging_rxon.cck_basic_rates =
		    (IWL_CCK_BASIC_RATES_MASK >> IWL_FIRST_CCK_RATE) & 0xF;

	if (priv->active_rate_basic & IWL_OFDM_BASIC_RATES_MASK)
		priv->staging_rxon.ofdm_basic_rates =
		    ((priv->active_rate_basic &
		      (IWL_OFDM_BASIC_RATES_MASK | IWL_RATE_6M_MASK)) >>
		      IWL_FIRST_OFDM_RATE) & 0xFF;
	else
		priv->staging_rxon.ofdm_basic_rates =
		   (IWL_OFDM_BASIC_RATES_MASK >> IWL_FIRST_OFDM_RATE) & 0xFF;
}

void iwl4965_radio_kill_sw(struct iwl_priv *priv, int disable_radio)
{
	unsigned long flags;

	if (!!disable_radio == test_bit(STATUS_RF_KILL_SW, &priv->status))
		return;

	IWL_DEBUG_RF_KILL("Manual SW RF KILL set to: RADIO %s\n",
			  disable_radio ? "OFF" : "ON");

	if (disable_radio) {
		iwl4965_scan_cancel(priv);
		/* FIXME: This is a workaround for AP */
		if (priv->iw_mode != IEEE80211_IF_TYPE_AP) {
			spin_lock_irqsave(&priv->lock, flags);
			iwl_write32(priv, CSR_UCODE_DRV_GP1_SET,
				    CSR_UCODE_SW_BIT_RFKILL);
			spin_unlock_irqrestore(&priv->lock, flags);
			/* call the host command only if no hw rf-kill set */
			if (!test_bit(STATUS_RF_KILL_HW, &priv->status) &&
			    iwl_is_ready(priv))
				iwl4965_send_card_state(priv,
							CARD_STATE_CMD_DISABLE,
							0);
			set_bit(STATUS_RF_KILL_SW, &priv->status);

			/* make sure mac80211 stop sending Tx frame */
			if (priv->mac80211_registered)
				ieee80211_stop_queues(priv->hw);
		}
		return;
	}

	spin_lock_irqsave(&priv->lock, flags);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);

	clear_bit(STATUS_RF_KILL_SW, &priv->status);
	spin_unlock_irqrestore(&priv->lock, flags);

	/* wake up ucode */
	msleep(10);

	spin_lock_irqsave(&priv->lock, flags);
	iwl_read32(priv, CSR_UCODE_DRV_GP1);
	if (!iwl_grab_nic_access(priv))
		iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (test_bit(STATUS_RF_KILL_HW, &priv->status)) {
		IWL_DEBUG_RF_KILL("Can not turn radio back on - "
				  "disabled by HW switch\n");
		return;
	}

	queue_work(priv->workqueue, &priv->restart);
	return;
}

void iwl4965_set_decrypted_flag(struct iwl_priv *priv, struct sk_buff *skb,
			    u32 decrypt_res, struct ieee80211_rx_status *stats)
{
	u16 fc =
	    le16_to_cpu(((struct ieee80211_hdr *)skb->data)->frame_control);

	if (priv->active_rxon.filter_flags & RXON_FILTER_DIS_DECRYPT_MSK)
		return;

	if (!(fc & IEEE80211_FCTL_PROTECTED))
		return;

	IWL_DEBUG_RX("decrypt_res:0x%x\n", decrypt_res);
	switch (decrypt_res & RX_RES_STATUS_SEC_TYPE_MSK) {
	case RX_RES_STATUS_SEC_TYPE_TKIP:
		/* The uCode has got a bad phase 1 Key, pushes the packet.
		 * Decryption will be done in SW. */
		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_BAD_KEY_TTAK)
			break;

		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_BAD_ICV_MIC)
			stats->flag |= RX_FLAG_MMIC_ERROR;
	case RX_RES_STATUS_SEC_TYPE_WEP:
	case RX_RES_STATUS_SEC_TYPE_CCMP:
		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_DECRYPT_OK) {
			IWL_DEBUG_RX("hw decrypt successfully!!!\n");
			stats->flag |= RX_FLAG_DECRYPTED;
		}
		break;

	default:
		break;
	}
}


#define IWL_PACKET_RETRY_TIME HZ

int iwl4965_is_duplicate_packet(struct iwl_priv *priv, struct ieee80211_hdr *header)
{
	u16 sc = le16_to_cpu(header->seq_ctrl);
	u16 seq = (sc & IEEE80211_SCTL_SEQ) >> 4;
	u16 frag = sc & IEEE80211_SCTL_FRAG;
	u16 *last_seq, *last_frag;
	unsigned long *last_time;

	switch (priv->iw_mode) {
	case IEEE80211_IF_TYPE_IBSS:{
		struct list_head *p;
		struct iwl4965_ibss_seq *entry = NULL;
		u8 *mac = header->addr2;
		int index = mac[5] & (IWL_IBSS_MAC_HASH_SIZE - 1);

		__list_for_each(p, &priv->ibss_mac_hash[index]) {
			entry = list_entry(p, struct iwl4965_ibss_seq, list);
			if (!compare_ether_addr(entry->mac, mac))
				break;
		}
		if (p == &priv->ibss_mac_hash[index]) {
			entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
			if (!entry) {
				IWL_ERROR("Cannot malloc new mac entry\n");
				return 0;
			}
			memcpy(entry->mac, mac, ETH_ALEN);
			entry->seq_num = seq;
			entry->frag_num = frag;
			entry->packet_time = jiffies;
			list_add(&entry->list, &priv->ibss_mac_hash[index]);
			return 0;
		}
		last_seq = &entry->seq_num;
		last_frag = &entry->frag_num;
		last_time = &entry->packet_time;
		break;
	}
	case IEEE80211_IF_TYPE_STA:
		last_seq = &priv->last_seq_num;
		last_frag = &priv->last_frag_num;
		last_time = &priv->last_packet_time;
		break;
	default:
		return 0;
	}
	if ((*last_seq == seq) &&
	    time_after(*last_time + IWL_PACKET_RETRY_TIME, jiffies)) {
		if (*last_frag == frag)
			goto drop;
		if (*last_frag + 1 != frag)
			/* out-of-order fragment */
			goto drop;
	} else
		*last_seq = seq;

	*last_frag = frag;
	*last_time = jiffies;
	return 0;

 drop:
	return 1;
}

#ifdef CONFIG_IWL4965_SPECTRUM_MEASUREMENT

#include "iwl-spectrum.h"

#define BEACON_TIME_MASK_LOW	0x00FFFFFF
#define BEACON_TIME_MASK_HIGH	0xFF000000
#define TIME_UNIT		1024

/*
 * extended beacon time format
 * time in usec will be changed into a 32-bit value in 8:24 format
 * the high 1 byte is the beacon counts
 * the lower 3 bytes is the time in usec within one beacon interval
 */

static u32 iwl4965_usecs_to_beacons(u32 usec, u32 beacon_interval)
{
	u32 quot;
	u32 rem;
	u32 interval = beacon_interval * 1024;

	if (!interval || !usec)
		return 0;

	quot = (usec / interval) & (BEACON_TIME_MASK_HIGH >> 24);
	rem = (usec % interval) & BEACON_TIME_MASK_LOW;

	return (quot << 24) + rem;
}

/* base is usually what we get from ucode with each received frame,
 * the same as HW timer counter counting down
 */

static __le32 iwl4965_add_beacon_time(u32 base, u32 addon, u32 beacon_interval)
{
	u32 base_low = base & BEACON_TIME_MASK_LOW;
	u32 addon_low = addon & BEACON_TIME_MASK_LOW;
	u32 interval = beacon_interval * TIME_UNIT;
	u32 res = (base & BEACON_TIME_MASK_HIGH) +
	    (addon & BEACON_TIME_MASK_HIGH);

	if (base_low > addon_low)
		res += base_low - addon_low;
	else if (base_low < addon_low) {
		res += interval + base_low - addon_low;
		res += (1 << 24);
	} else
		res += (1 << 24);

	return cpu_to_le32(res);
}

static int iwl4965_get_measurement(struct iwl_priv *priv,
			       struct ieee80211_measurement_params *params,
			       u8 type)
{
	struct iwl4965_spectrum_cmd spectrum;
	struct iwl4965_rx_packet *res;
	struct iwl_host_cmd cmd = {
		.id = REPLY_SPECTRUM_MEASUREMENT_CMD,
		.data = (void *)&spectrum,
		.meta.flags = CMD_WANT_SKB,
	};
	u32 add_time = le64_to_cpu(params->start_time);
	int rc;
	int spectrum_resp_status;
	int duration = le16_to_cpu(params->duration);

	if (iwl_is_associated(priv))
		add_time =
		    iwl4965_usecs_to_beacons(
			le64_to_cpu(params->start_time) - priv->last_tsf,
			le16_to_cpu(priv->rxon_timing.beacon_interval));

	memset(&spectrum, 0, sizeof(spectrum));

	spectrum.channel_count = cpu_to_le16(1);
	spectrum.flags =
	    RXON_FLG_TSF2HOST_MSK | RXON_FLG_ANT_A_MSK | RXON_FLG_DIS_DIV_MSK;
	spectrum.filter_flags = MEASUREMENT_FILTER_FLAG;
	cmd.len = sizeof(spectrum);
	spectrum.len = cpu_to_le16(cmd.len - sizeof(spectrum.len));

	if (iwl_is_associated(priv))
		spectrum.start_time =
		    iwl4965_add_beacon_time(priv->last_beacon_time,
				add_time,
				le16_to_cpu(priv->rxon_timing.beacon_interval));
	else
		spectrum.start_time = 0;

	spectrum.channels[0].duration = cpu_to_le32(duration * TIME_UNIT);
	spectrum.channels[0].channel = params->channel;
	spectrum.channels[0].type = type;
	if (priv->active_rxon.flags & RXON_FLG_BAND_24G_MSK)
		spectrum.flags |= RXON_FLG_BAND_24G_MSK |
		    RXON_FLG_AUTO_DETECT_MSK | RXON_FLG_TGG_PROTECT_MSK;

	rc = iwl_send_cmd_sync(priv, &cmd);
	if (rc)
		return rc;

	res = (struct iwl4965_rx_packet *)cmd.meta.u.skb->data;
	if (res->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERROR("Bad return from REPLY_RX_ON_ASSOC command\n");
		rc = -EIO;
	}

	spectrum_resp_status = le16_to_cpu(res->u.spectrum.status);
	switch (spectrum_resp_status) {
	case 0:		/* Command will be handled */
		if (res->u.spectrum.id != 0xff) {
			IWL_DEBUG_INFO
			    ("Replaced existing measurement: %d\n",
			     res->u.spectrum.id);
			priv->measurement_status &= ~MEASUREMENT_READY;
		}
		priv->measurement_status |= MEASUREMENT_ACTIVE;
		rc = 0;
		break;

	case 1:		/* Command will not be handled */
		rc = -EAGAIN;
		break;
	}

	dev_kfree_skb_any(cmd.meta.u.skb);

	return rc;
}
#endif

static void iwl4965_txstatus_to_ieee(struct iwl_priv *priv,
				 struct iwl4965_tx_info *tx_sta)
{

	tx_sta->status.ack_signal = 0;
	tx_sta->status.excessive_retries = 0;
	tx_sta->status.queue_length = 0;
	tx_sta->status.queue_number = 0;

	if (in_interrupt())
		ieee80211_tx_status_irqsafe(priv->hw,
					    tx_sta->skb[0], &(tx_sta->status));
	else
		ieee80211_tx_status(priv->hw,
				    tx_sta->skb[0], &(tx_sta->status));

	tx_sta->skb[0] = NULL;
}

/**
 * iwl4965_tx_queue_reclaim - Reclaim Tx queue entries already Tx'd
 *
 * When FW advances 'R' index, all entries between old and new 'R' index
 * need to be reclaimed. As result, some free space forms.  If there is
 * enough free space (> low mark), wake the stack that feeds us.
 */
int iwl4965_tx_queue_reclaim(struct iwl_priv *priv, int txq_id, int index)
{
	struct iwl4965_tx_queue *txq = &priv->txq[txq_id];
	struct iwl4965_queue *q = &txq->q;
	int nfreed = 0;

	if ((index >= q->n_bd) || (x2_queue_used(q, index) == 0)) {
		IWL_ERROR("Read index for DMA queue txq id (%d), index %d, "
			  "is out of range [0-%d] %d %d.\n", txq_id,
			  index, q->n_bd, q->write_ptr, q->read_ptr);
		return 0;
	}

	for (index = iwl_queue_inc_wrap(index, q->n_bd);
		q->read_ptr != index;
		q->read_ptr = iwl_queue_inc_wrap(q->read_ptr, q->n_bd)) {
		if (txq_id != IWL_CMD_QUEUE_NUM) {
			iwl4965_txstatus_to_ieee(priv,
					&(txq->txb[txq->q.read_ptr]));
			iwl4965_hw_txq_free_tfd(priv, txq);
		} else if (nfreed > 1) {
			IWL_ERROR("HCMD skipped: index (%d) %d %d\n", index,
					q->write_ptr, q->read_ptr);
			queue_work(priv->workqueue, &priv->restart);
		}
		nfreed++;
	}

/*	if (iwl4965_queue_space(q) > q->low_mark && (txq_id >= 0) &&
			(txq_id != IWL_CMD_QUEUE_NUM) &&
			priv->mac80211_registered)
		ieee80211_wake_queue(priv->hw, txq_id); */


	return nfreed;
}

static int iwl4965_is_tx_success(u32 status)
{
	status &= TX_STATUS_MSK;
	return (status == TX_STATUS_SUCCESS)
	    || (status == TX_STATUS_DIRECT_DONE);
}

/******************************************************************************
 *
 * Generic RX handler implementations
 *
 ******************************************************************************/
#ifdef CONFIG_IWL4965_HT

static inline int iwl4965_get_ra_sta_id(struct iwl_priv *priv,
				    struct ieee80211_hdr *hdr)
{
	if (priv->iw_mode == IEEE80211_IF_TYPE_STA)
		return IWL_AP_ID;
	else {
		u8 *da = ieee80211_get_DA(hdr);
		return iwl4965_hw_find_station(priv, da);
	}
}

static struct ieee80211_hdr *iwl4965_tx_queue_get_hdr(
	struct iwl_priv *priv, int txq_id, int idx)
{
	if (priv->txq[txq_id].txb[idx].skb[0])
		return (struct ieee80211_hdr *)priv->txq[txq_id].
				txb[idx].skb[0]->data;
	return NULL;
}

static inline u32 iwl4965_get_scd_ssn(struct iwl4965_tx_resp *tx_resp)
{
	__le32 *scd_ssn = (__le32 *)((u32 *)&tx_resp->status +
				tx_resp->frame_count);
	return le32_to_cpu(*scd_ssn) & MAX_SN;

}

/**
 * iwl4965_tx_status_reply_tx - Handle Tx rspnse for frames in aggregation queue
 */
static int iwl4965_tx_status_reply_tx(struct iwl_priv *priv,
				      struct iwl4965_ht_agg *agg,
				      struct iwl4965_tx_resp_agg *tx_resp,
				      u16 start_idx)
{
	u16 status;
	struct agg_tx_status *frame_status = &tx_resp->status;
	struct ieee80211_tx_status *tx_status = NULL;
	struct ieee80211_hdr *hdr = NULL;
	int i, sh;
	int txq_id, idx;
	u16 seq;

	if (agg->wait_for_ba)
		IWL_DEBUG_TX_REPLY("got tx response w/o block-ack\n");

	agg->frame_count = tx_resp->frame_count;
	agg->start_idx = start_idx;
	agg->rate_n_flags = le32_to_cpu(tx_resp->rate_n_flags);
	agg->bitmap = 0;

	/* # frames attempted by Tx command */
	if (agg->frame_count == 1) {
		/* Only one frame was attempted; no block-ack will arrive */
		status = le16_to_cpu(frame_status[0].status);
		seq  = le16_to_cpu(frame_status[0].sequence);
		idx = SEQ_TO_INDEX(seq);
		txq_id = SEQ_TO_QUEUE(seq);

		/* FIXME: code repetition */
		IWL_DEBUG_TX_REPLY("FrameCnt = %d, StartIdx=%d idx=%d\n",
				   agg->frame_count, agg->start_idx, idx);

		tx_status = &(priv->txq[txq_id].txb[idx].status);
		tx_status->retry_count = tx_resp->failure_frame;
		tx_status->queue_number = status & 0xff;
		tx_status->queue_length = tx_resp->failure_rts;
		tx_status->control.flags &= ~IEEE80211_TXCTL_AMPDU;
		tx_status->flags = iwl4965_is_tx_success(status)?
			IEEE80211_TX_STATUS_ACK : 0;
		iwl4965_hwrate_to_tx_control(priv,
					     le32_to_cpu(tx_resp->rate_n_flags),
					     &tx_status->control);
		/* FIXME: code repetition end */

		IWL_DEBUG_TX_REPLY("1 Frame 0x%x failure :%d\n",
				    status & 0xff, tx_resp->failure_frame);
		IWL_DEBUG_TX_REPLY("Rate Info rate_n_flags=%x\n",
				iwl4965_hw_get_rate_n_flags(tx_resp->rate_n_flags));

		agg->wait_for_ba = 0;
	} else {
		/* Two or more frames were attempted; expect block-ack */
		u64 bitmap = 0;
		int start = agg->start_idx;

		/* Construct bit-map of pending frames within Tx window */
		for (i = 0; i < agg->frame_count; i++) {
			u16 sc;
			status = le16_to_cpu(frame_status[i].status);
			seq  = le16_to_cpu(frame_status[i].sequence);
			idx = SEQ_TO_INDEX(seq);
			txq_id = SEQ_TO_QUEUE(seq);

			if (status & (AGG_TX_STATE_FEW_BYTES_MSK |
				      AGG_TX_STATE_ABORT_MSK))
				continue;

			IWL_DEBUG_TX_REPLY("FrameCnt = %d, txq_id=%d idx=%d\n",
					   agg->frame_count, txq_id, idx);

			hdr = iwl4965_tx_queue_get_hdr(priv, txq_id, idx);

			sc = le16_to_cpu(hdr->seq_ctrl);
			if (idx != (SEQ_TO_SN(sc) & 0xff)) {
				IWL_ERROR("BUG_ON idx doesn't match seq control"
					  " idx=%d, seq_idx=%d, seq=%d\n",
					  idx, SEQ_TO_SN(sc),
					  hdr->seq_ctrl);
				return -1;
			}

			IWL_DEBUG_TX_REPLY("AGG Frame i=%d idx %d seq=%d\n",
					   i, idx, SEQ_TO_SN(sc));

			sh = idx - start;
			if (sh > 64) {
				sh = (start - idx) + 0xff;
				bitmap = bitmap << sh;
				sh = 0;
				start = idx;
			} else if (sh < -64)
				sh  = 0xff - (start - idx);
			else if (sh < 0) {
				sh = start - idx;
				start = idx;
				bitmap = bitmap << sh;
				sh = 0;
			}
			bitmap |= (1 << sh);
			IWL_DEBUG_TX_REPLY("start=%d bitmap=0x%x\n",
					   start, (u32)(bitmap & 0xFFFFFFFF));
		}

		agg->bitmap = bitmap;
		agg->start_idx = start;
		agg->rate_n_flags = le32_to_cpu(tx_resp->rate_n_flags);
		IWL_DEBUG_TX_REPLY("Frames %d start_idx=%d bitmap=0x%llx\n",
				   agg->frame_count, agg->start_idx,
				   (unsigned long long)agg->bitmap);

		if (bitmap)
			agg->wait_for_ba = 1;
	}
	return 0;
}
#endif

/**
 * iwl4965_rx_reply_tx - Handle standard (non-aggregation) Tx response
 */
static void iwl4965_rx_reply_tx(struct iwl_priv *priv,
			    struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	int index = SEQ_TO_INDEX(sequence);
	struct iwl4965_tx_queue *txq = &priv->txq[txq_id];
	struct ieee80211_tx_status *tx_status;
	struct iwl4965_tx_resp *tx_resp = (void *)&pkt->u.raw[0];
	u32  status = le32_to_cpu(tx_resp->status);
#ifdef CONFIG_IWL4965_HT
	int tid = MAX_TID_COUNT, sta_id = IWL_INVALID_STATION;
	struct ieee80211_hdr *hdr;
	__le16 *qc;
#endif

	if ((index >= txq->q.n_bd) || (x2_queue_used(&txq->q, index) == 0)) {
		IWL_ERROR("Read index for DMA queue txq_id (%d) index %d "
			  "is out of range [0-%d] %d %d\n", txq_id,
			  index, txq->q.n_bd, txq->q.write_ptr,
			  txq->q.read_ptr);
		return;
	}

#ifdef CONFIG_IWL4965_HT
	hdr = iwl4965_tx_queue_get_hdr(priv, txq_id, index);
	qc = ieee80211_get_qos_ctrl(hdr);

	if (qc)
		tid = le16_to_cpu(*qc) & 0xf;

	sta_id = iwl4965_get_ra_sta_id(priv, hdr);
	if (txq->sched_retry && unlikely(sta_id == IWL_INVALID_STATION)) {
		IWL_ERROR("Station not known\n");
		return;
	}

	if (txq->sched_retry) {
		const u32 scd_ssn = iwl4965_get_scd_ssn(tx_resp);
		struct iwl4965_ht_agg *agg = NULL;

		if (!qc)
			return;

		agg = &priv->stations[sta_id].tid[tid].agg;

		iwl4965_tx_status_reply_tx(priv, agg,
				(struct iwl4965_tx_resp_agg *)tx_resp, index);

		if ((tx_resp->frame_count == 1) &&
		    !iwl4965_is_tx_success(status)) {
			/* TODO: send BAR */
		}

		if (txq->q.read_ptr != (scd_ssn & 0xff)) {
			int freed;
			index = iwl_queue_dec_wrap(scd_ssn & 0xff, txq->q.n_bd);
			IWL_DEBUG_TX_REPLY("Retry scheduler reclaim scd_ssn "
					   "%d index %d\n", scd_ssn , index);
			freed = iwl4965_tx_queue_reclaim(priv, txq_id, index);
			priv->stations[sta_id].tid[tid].tfds_in_queue -= freed;

			if (iwl4965_queue_space(&txq->q) > txq->q.low_mark &&
			    txq_id >= 0 && priv->mac80211_registered &&
			    agg->state != IWL_EMPTYING_HW_QUEUE_DELBA)
				ieee80211_wake_queue(priv->hw, txq_id);

			iwl4965_check_empty_hw_queue(priv, sta_id, tid, txq_id);
		}
	} else {
#endif /* CONFIG_IWL4965_HT */
	tx_status = &(txq->txb[txq->q.read_ptr].status);

	tx_status->retry_count = tx_resp->failure_frame;
	tx_status->queue_number = status;
	tx_status->queue_length = tx_resp->bt_kill_count;
	tx_status->queue_length |= tx_resp->failure_rts;
	tx_status->flags =
	    iwl4965_is_tx_success(status) ? IEEE80211_TX_STATUS_ACK : 0;
	iwl4965_hwrate_to_tx_control(priv, le32_to_cpu(tx_resp->rate_n_flags),
				     &tx_status->control);

	IWL_DEBUG_TX("Tx queue %d Status %s (0x%08x) rate_n_flags 0x%x "
		     "retries %d\n", txq_id, iwl4965_get_tx_fail_reason(status),
		     status, le32_to_cpu(tx_resp->rate_n_flags),
		     tx_resp->failure_frame);

	IWL_DEBUG_TX_REPLY("Tx queue reclaim %d\n", index);
	if (index != -1) {
		int freed = iwl4965_tx_queue_reclaim(priv, txq_id, index);
#ifdef CONFIG_IWL4965_HT
		if (tid != MAX_TID_COUNT)
			priv->stations[sta_id].tid[tid].tfds_in_queue -= freed;
		if (iwl4965_queue_space(&txq->q) > txq->q.low_mark &&
			(txq_id >= 0) &&
			priv->mac80211_registered)
			ieee80211_wake_queue(priv->hw, txq_id);
		if (tid != MAX_TID_COUNT)
			iwl4965_check_empty_hw_queue(priv, sta_id, tid, txq_id);
#endif
	}
#ifdef CONFIG_IWL4965_HT
	}
#endif /* CONFIG_IWL4965_HT */

	if (iwl_check_bits(status, TX_ABORT_REQUIRED_MSK))
		IWL_ERROR("TODO:  Implement Tx ABORT REQUIRED!!!\n");
}


static void iwl4965_rx_reply_alive(struct iwl_priv *priv,
			       struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl4965_alive_resp *palive;
	struct delayed_work *pwork;

	palive = &pkt->u.alive_frame;

	IWL_DEBUG_INFO("Alive ucode status 0x%08X revision "
		       "0x%01X 0x%01X\n",
		       palive->is_valid, palive->ver_type,
		       palive->ver_subtype);

	if (palive->ver_subtype == INITIALIZE_SUBTYPE) {
		IWL_DEBUG_INFO("Initialization Alive received.\n");
		memcpy(&priv->card_alive_init,
		       &pkt->u.alive_frame,
		       sizeof(struct iwl4965_init_alive_resp));
		pwork = &priv->init_alive_start;
	} else {
		IWL_DEBUG_INFO("Runtime Alive received.\n");
		memcpy(&priv->card_alive, &pkt->u.alive_frame,
		       sizeof(struct iwl4965_alive_resp));
		pwork = &priv->alive_start;
	}

	/* We delay the ALIVE response by 5ms to
	 * give the HW RF Kill time to activate... */
	if (palive->is_valid == UCODE_VALID_OK)
		queue_delayed_work(priv->workqueue, pwork,
				   msecs_to_jiffies(5));
	else
		IWL_WARNING("uCode did not respond OK.\n");
}

static void iwl4965_rx_reply_add_sta(struct iwl_priv *priv,
				 struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;

	IWL_DEBUG_RX("Received REPLY_ADD_STA: 0x%02X\n", pkt->u.status);
	return;
}

static void iwl4965_rx_reply_error(struct iwl_priv *priv,
			       struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;

	IWL_ERROR("Error Reply type 0x%08X cmd %s (0x%02X) "
		"seq 0x%04X ser 0x%08X\n",
		le32_to_cpu(pkt->u.err_resp.error_type),
		get_cmd_string(pkt->u.err_resp.cmd_id),
		pkt->u.err_resp.cmd_id,
		le16_to_cpu(pkt->u.err_resp.bad_cmd_seq_num),
		le32_to_cpu(pkt->u.err_resp.error_info));
}

#define TX_STATUS_ENTRY(x) case TX_STATUS_FAIL_ ## x: return #x

static void iwl4965_rx_csa(struct iwl_priv *priv, struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl4965_rxon_cmd *rxon = (void *)&priv->active_rxon;
	struct iwl4965_csa_notification *csa = &(pkt->u.csa_notif);
	IWL_DEBUG_11H("CSA notif: channel %d, status %d\n",
		      le16_to_cpu(csa->channel), le32_to_cpu(csa->status));
	rxon->channel = csa->channel;
	priv->staging_rxon.channel = csa->channel;
}

static void iwl4965_rx_spectrum_measure_notif(struct iwl_priv *priv,
					  struct iwl4965_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWL4965_SPECTRUM_MEASUREMENT
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl4965_spectrum_notification *report = &(pkt->u.spectrum_notif);

	if (!report->state) {
		IWL_DEBUG(IWL_DL_11H | IWL_DL_INFO,
			  "Spectrum Measure Notification: Start\n");
		return;
	}

	memcpy(&priv->measure_report, report, sizeof(*report));
	priv->measurement_status |= MEASUREMENT_READY;
#endif
}

static void iwl4965_rx_pm_sleep_notif(struct iwl_priv *priv,
				  struct iwl4965_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl4965_sleep_notification *sleep = &(pkt->u.sleep_notif);
	IWL_DEBUG_RX("sleep mode: %d, src: %d\n",
		     sleep->pm_sleep_mode, sleep->pm_wakeup_src);
#endif
}

static void iwl4965_rx_pm_debug_statistics_notif(struct iwl_priv *priv,
					     struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	IWL_DEBUG_RADIO("Dumping %d bytes of unhandled "
			"notification for %s:\n",
			le32_to_cpu(pkt->len), get_cmd_string(pkt->hdr.cmd));
	iwl_print_hex_dump(IWL_DL_RADIO, pkt->u.raw, le32_to_cpu(pkt->len));
}

static void iwl4965_bg_beacon_update(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, beacon_update);
	struct sk_buff *beacon;

	/* Pull updated AP beacon from mac80211. will fail if not in AP mode */
	beacon = ieee80211_beacon_get(priv->hw, priv->vif, NULL);

	if (!beacon) {
		IWL_ERROR("update beacon failed\n");
		return;
	}

	mutex_lock(&priv->mutex);
	/* new beacon skb is allocated every time; dispose previous.*/
	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = beacon;
	mutex_unlock(&priv->mutex);

	iwl4965_send_beacon_cmd(priv);
}

static void iwl4965_rx_beacon_notif(struct iwl_priv *priv,
				struct iwl4965_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl4965_beacon_notif *beacon = &(pkt->u.beacon_status);
	u8 rate = iwl4965_hw_get_rate(beacon->beacon_notify_hdr.rate_n_flags);

	IWL_DEBUG_RX("beacon status %x retries %d iss %d "
		"tsf %d %d rate %d\n",
		le32_to_cpu(beacon->beacon_notify_hdr.status) & TX_STATUS_MSK,
		beacon->beacon_notify_hdr.failure_frame,
		le32_to_cpu(beacon->ibss_mgr_status),
		le32_to_cpu(beacon->high_tsf),
		le32_to_cpu(beacon->low_tsf), rate);
#endif

	if ((priv->iw_mode == IEEE80211_IF_TYPE_AP) &&
	    (!test_bit(STATUS_EXIT_PENDING, &priv->status)))
		queue_work(priv->workqueue, &priv->beacon_update);
}

/* Service response to REPLY_SCAN_CMD (0x80) */
static void iwl4965_rx_reply_scan(struct iwl_priv *priv,
			      struct iwl4965_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl4965_scanreq_notification *notif =
	    (struct iwl4965_scanreq_notification *)pkt->u.raw;

	IWL_DEBUG_RX("Scan request status = 0x%x\n", notif->status);
#endif
}

/* Service SCAN_START_NOTIFICATION (0x82) */
static void iwl4965_rx_scan_start_notif(struct iwl_priv *priv,
				    struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl4965_scanstart_notification *notif =
	    (struct iwl4965_scanstart_notification *)pkt->u.raw;
	priv->scan_start_tsf = le32_to_cpu(notif->tsf_low);
	IWL_DEBUG_SCAN("Scan start: "
		       "%d [802.11%s] "
		       "(TSF: 0x%08X:%08X) - %d (beacon timer %u)\n",
		       notif->channel,
		       notif->band ? "bg" : "a",
		       notif->tsf_high,
		       notif->tsf_low, notif->status, notif->beacon_timer);
}

/* Service SCAN_RESULTS_NOTIFICATION (0x83) */
static void iwl4965_rx_scan_results_notif(struct iwl_priv *priv,
				      struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl4965_scanresults_notification *notif =
	    (struct iwl4965_scanresults_notification *)pkt->u.raw;

	IWL_DEBUG_SCAN("Scan ch.res: "
		       "%d [802.11%s] "
		       "(TSF: 0x%08X:%08X) - %d "
		       "elapsed=%lu usec (%dms since last)\n",
		       notif->channel,
		       notif->band ? "bg" : "a",
		       le32_to_cpu(notif->tsf_high),
		       le32_to_cpu(notif->tsf_low),
		       le32_to_cpu(notif->statistics[0]),
		       le32_to_cpu(notif->tsf_low) - priv->scan_start_tsf,
		       jiffies_to_msecs(elapsed_jiffies
					(priv->last_scan_jiffies, jiffies)));

	priv->last_scan_jiffies = jiffies;
	priv->next_scan_jiffies = 0;
}

/* Service SCAN_COMPLETE_NOTIFICATION (0x84) */
static void iwl4965_rx_scan_complete_notif(struct iwl_priv *priv,
				       struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl4965_scancomplete_notification *scan_notif = (void *)pkt->u.raw;

	IWL_DEBUG_SCAN("Scan complete: %d channels (TSF 0x%08X:%08X) - %d\n",
		       scan_notif->scanned_channels,
		       scan_notif->tsf_low,
		       scan_notif->tsf_high, scan_notif->status);

	/* The HW is no longer scanning */
	clear_bit(STATUS_SCAN_HW, &priv->status);

	/* The scan completion notification came in, so kill that timer... */
	cancel_delayed_work(&priv->scan_check);

	IWL_DEBUG_INFO("Scan pass on %sGHz took %dms\n",
		       (priv->scan_bands == 2) ? "2.4" : "5.2",
		       jiffies_to_msecs(elapsed_jiffies
					(priv->scan_pass_start, jiffies)));

	/* Remove this scanned band from the list
	 * of pending bands to scan */
	priv->scan_bands--;

	/* If a request to abort was given, or the scan did not succeed
	 * then we reset the scan state machine and terminate,
	 * re-queuing another scan if one has been requested */
	if (test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_INFO("Aborted scan completed.\n");
		clear_bit(STATUS_SCAN_ABORTING, &priv->status);
	} else {
		/* If there are more bands on this scan pass reschedule */
		if (priv->scan_bands > 0)
			goto reschedule;
	}

	priv->last_scan_jiffies = jiffies;
	priv->next_scan_jiffies = 0;
	IWL_DEBUG_INFO("Setting scan to off\n");

	clear_bit(STATUS_SCANNING, &priv->status);

	IWL_DEBUG_INFO("Scan took %dms\n",
		jiffies_to_msecs(elapsed_jiffies(priv->scan_start, jiffies)));

	queue_work(priv->workqueue, &priv->scan_completed);

	return;

reschedule:
	priv->scan_pass_start = jiffies;
	queue_work(priv->workqueue, &priv->request_scan);
}

/* Handle notification from uCode that card's power state is changing
 * due to software, hardware, or critical temperature RFKILL */
static void iwl4965_rx_card_state_notif(struct iwl_priv *priv,
				    struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (void *)rxb->skb->data;
	u32 flags = le32_to_cpu(pkt->u.card_state_notif.flags);
	unsigned long status = priv->status;

	IWL_DEBUG_RF_KILL("Card state received: HW:%s SW:%s\n",
			  (flags & HW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & SW_CARD_DISABLED) ? "Kill" : "On");

	if (flags & (SW_CARD_DISABLED | HW_CARD_DISABLED |
		     RF_CARD_DISABLED)) {

		iwl_write32(priv, CSR_UCODE_DRV_GP1_SET,
			    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

		if (!iwl_grab_nic_access(priv)) {
			iwl_write_direct32(
				priv, HBUS_TARG_MBX_C,
				HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED);

			iwl_release_nic_access(priv);
		}

		if (!(flags & RXON_CARD_DISABLED)) {
			iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
				    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);
			if (!iwl_grab_nic_access(priv)) {
				iwl_write_direct32(
					priv, HBUS_TARG_MBX_C,
					HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED);

				iwl_release_nic_access(priv);
			}
		}

		if (flags & RF_CARD_DISABLED) {
			iwl_write32(priv, CSR_UCODE_DRV_GP1_SET,
				    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
			iwl_read32(priv, CSR_UCODE_DRV_GP1);
			if (!iwl_grab_nic_access(priv))
				iwl_release_nic_access(priv);
		}
	}

	if (flags & HW_CARD_DISABLED)
		set_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		clear_bit(STATUS_RF_KILL_HW, &priv->status);


	if (flags & SW_CARD_DISABLED)
		set_bit(STATUS_RF_KILL_SW, &priv->status);
	else
		clear_bit(STATUS_RF_KILL_SW, &priv->status);

	if (!(flags & RXON_CARD_DISABLED))
		iwl4965_scan_cancel(priv);

	if ((test_bit(STATUS_RF_KILL_HW, &status) !=
	     test_bit(STATUS_RF_KILL_HW, &priv->status)) ||
	    (test_bit(STATUS_RF_KILL_SW, &status) !=
	     test_bit(STATUS_RF_KILL_SW, &priv->status)))
		queue_work(priv->workqueue, &priv->rf_kill);
	else
		wake_up_interruptible(&priv->wait_command_queue);
}

/**
 * iwl4965_setup_rx_handlers - Initialize Rx handler callbacks
 *
 * Setup the RX handlers for each of the reply types sent from the uCode
 * to the host.
 *
 * This function chains into the hardware specific files for them to setup
 * any hardware specific handlers as well.
 */
static void iwl4965_setup_rx_handlers(struct iwl_priv *priv)
{
	priv->rx_handlers[REPLY_ALIVE] = iwl4965_rx_reply_alive;
	priv->rx_handlers[REPLY_ADD_STA] = iwl4965_rx_reply_add_sta;
	priv->rx_handlers[REPLY_ERROR] = iwl4965_rx_reply_error;
	priv->rx_handlers[CHANNEL_SWITCH_NOTIFICATION] = iwl4965_rx_csa;
	priv->rx_handlers[SPECTRUM_MEASURE_NOTIFICATION] =
	    iwl4965_rx_spectrum_measure_notif;
	priv->rx_handlers[PM_SLEEP_NOTIFICATION] = iwl4965_rx_pm_sleep_notif;
	priv->rx_handlers[PM_DEBUG_STATISTIC_NOTIFIC] =
	    iwl4965_rx_pm_debug_statistics_notif;
	priv->rx_handlers[BEACON_NOTIFICATION] = iwl4965_rx_beacon_notif;

	/*
	 * The same handler is used for both the REPLY to a discrete
	 * statistics request from the host as well as for the periodic
	 * statistics notifications (after received beacons) from the uCode.
	 */
	priv->rx_handlers[REPLY_STATISTICS_CMD] = iwl4965_hw_rx_statistics;
	priv->rx_handlers[STATISTICS_NOTIFICATION] = iwl4965_hw_rx_statistics;

	priv->rx_handlers[REPLY_SCAN_CMD] = iwl4965_rx_reply_scan;
	priv->rx_handlers[SCAN_START_NOTIFICATION] = iwl4965_rx_scan_start_notif;
	priv->rx_handlers[SCAN_RESULTS_NOTIFICATION] =
	    iwl4965_rx_scan_results_notif;
	priv->rx_handlers[SCAN_COMPLETE_NOTIFICATION] =
	    iwl4965_rx_scan_complete_notif;
	priv->rx_handlers[CARD_STATE_NOTIFICATION] = iwl4965_rx_card_state_notif;
	priv->rx_handlers[REPLY_TX] = iwl4965_rx_reply_tx;

	/* Set up hardware specific Rx handlers */
	iwl4965_hw_rx_handler_setup(priv);
}

/**
 * iwl4965_tx_cmd_complete - Pull unused buffers off the queue and reclaim them
 * @rxb: Rx buffer to reclaim
 *
 * If an Rx buffer has an async callback associated with it the callback
 * will be executed.  The attached skb (if present) will only be freed
 * if the callback returns 1
 */
static void iwl4965_tx_cmd_complete(struct iwl_priv *priv,
				struct iwl4965_rx_mem_buffer *rxb)
{
	struct iwl4965_rx_packet *pkt = (struct iwl4965_rx_packet *)rxb->skb->data;
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	int index = SEQ_TO_INDEX(sequence);
	int huge = sequence & SEQ_HUGE_FRAME;
	int cmd_index;
	struct iwl_cmd *cmd;

	/* If a Tx command is being handled and it isn't in the actual
	 * command queue then there a command routing bug has been introduced
	 * in the queue management code. */
	if (txq_id != IWL_CMD_QUEUE_NUM)
		IWL_ERROR("Error wrong command queue %d command id 0x%X\n",
			  txq_id, pkt->hdr.cmd);
	BUG_ON(txq_id != IWL_CMD_QUEUE_NUM);

	cmd_index = get_cmd_index(&priv->txq[IWL_CMD_QUEUE_NUM].q, index, huge);
	cmd = &priv->txq[IWL_CMD_QUEUE_NUM].cmd[cmd_index];

	/* Input error checking is done when commands are added to queue. */
	if (cmd->meta.flags & CMD_WANT_SKB) {
		cmd->meta.source->u.skb = rxb->skb;
		rxb->skb = NULL;
	} else if (cmd->meta.u.callback &&
		   !cmd->meta.u.callback(priv, cmd, rxb->skb))
		rxb->skb = NULL;

	iwl4965_tx_queue_reclaim(priv, txq_id, index);

	if (!(cmd->meta.flags & CMD_ASYNC)) {
		clear_bit(STATUS_HCMD_ACTIVE, &priv->status);
		wake_up_interruptible(&priv->wait_command_queue);
	}
}

/************************** RX-FUNCTIONS ****************************/
/*
 * Rx theory of operation
 *
 * Driver allocates a circular buffer of Receive Buffer Descriptors (RBDs),
 * each of which point to Receive Buffers to be filled by 4965.  These get
 * used not only for Rx frames, but for any command response or notification
 * from the 4965.  The driver and 4965 manage the Rx buffers by means
 * of indexes into the circular buffer.
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
 * + In iwl4965_rx_replenish (scheduled) if 'processed' != 'read' then the
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
 * iwl4965_rx_queue_alloc()   Allocates rx_free
 * iwl4965_rx_replenish()     Replenishes rx_free list from rx_used, and calls
 *                            iwl4965_rx_queue_restock
 * iwl4965_rx_queue_restock() Moves available buffers from rx_free into Rx
 *                            queue, updates firmware pointers, and updates
 *                            the WRITE index.  If insufficient rx_free buffers
 *                            are available, schedules iwl4965_rx_replenish
 *
 * -- enable interrupts --
 * ISR - iwl4965_rx()         Detach iwl4965_rx_mem_buffers from pool up to the
 *                            READ INDEX, detaching the SKB from the pool.
 *                            Moves the packet buffer from queue to rx_used.
 *                            Calls iwl4965_rx_queue_restock to refill any empty
 *                            slots.
 * ...
 *
 */

/**
 * iwl4965_rx_queue_space - Return number of free slots available in queue.
 */
static int iwl4965_rx_queue_space(const struct iwl4965_rx_queue *q)
{
	int s = q->read - q->write;
	if (s <= 0)
		s += RX_QUEUE_SIZE;
	/* keep some buffer to not confuse full and empty queue */
	s -= 2;
	if (s < 0)
		s = 0;
	return s;
}

/**
 * iwl4965_rx_queue_update_write_ptr - Update the write pointer for the RX queue
 */
int iwl4965_rx_queue_update_write_ptr(struct iwl_priv *priv, struct iwl4965_rx_queue *q)
{
	u32 reg = 0;
	int rc = 0;
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);

	if (q->need_update == 0)
		goto exit_unlock;

	/* If power-saving is in use, make sure device is awake */
	if (test_bit(STATUS_POWER_PMI, &priv->status)) {
		reg = iwl_read32(priv, CSR_UCODE_DRV_GP1);

		if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
			iwl_set_bit(priv, CSR_GP_CNTRL,
				    CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
			goto exit_unlock;
		}

		rc = iwl_grab_nic_access(priv);
		if (rc)
			goto exit_unlock;

		/* Device expects a multiple of 8 */
		iwl_write_direct32(priv, FH_RSCSR_CHNL0_WPTR,
				     q->write & ~0x7);
		iwl_release_nic_access(priv);

	/* Else device is assumed to be awake */
	} else
		/* Device expects a multiple of 8 */
		iwl_write32(priv, FH_RSCSR_CHNL0_WPTR, q->write & ~0x7);


	q->need_update = 0;

 exit_unlock:
	spin_unlock_irqrestore(&q->lock, flags);
	return rc;
}

/**
 * iwl4965_dma_addr2rbd_ptr - convert a DMA address to a uCode read buffer ptr
 */
static inline __le32 iwl4965_dma_addr2rbd_ptr(struct iwl_priv *priv,
					  dma_addr_t dma_addr)
{
	return cpu_to_le32((u32)(dma_addr >> 8));
}


/**
 * iwl4965_rx_queue_restock - refill RX queue from pre-allocated pool
 *
 * If there are slots in the RX queue that need to be restocked,
 * and we have free pre-allocated buffers, fill the ranks as much
 * as we can, pulling from rx_free.
 *
 * This moves the 'write' index forward to catch up with 'processed', and
 * also updates the memory address in the firmware to reference the new
 * target buffer.
 */
static int iwl4965_rx_queue_restock(struct iwl_priv *priv)
{
	struct iwl4965_rx_queue *rxq = &priv->rxq;
	struct list_head *element;
	struct iwl4965_rx_mem_buffer *rxb;
	unsigned long flags;
	int write, rc;

	spin_lock_irqsave(&rxq->lock, flags);
	write = rxq->write & ~0x7;
	while ((iwl4965_rx_queue_space(rxq) > 0) && (rxq->free_count)) {
		/* Get next free Rx buffer, remove from free list */
		element = rxq->rx_free.next;
		rxb = list_entry(element, struct iwl4965_rx_mem_buffer, list);
		list_del(element);

		/* Point to Rx buffer via next RBD in circular buffer */
		rxq->bd[rxq->write] = iwl4965_dma_addr2rbd_ptr(priv, rxb->dma_addr);
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
	if ((write != (rxq->write & ~0x7))
	    || (abs(rxq->write - rxq->read) > 7)) {
		spin_lock_irqsave(&rxq->lock, flags);
		rxq->need_update = 1;
		spin_unlock_irqrestore(&rxq->lock, flags);
		rc = iwl4965_rx_queue_update_write_ptr(priv, rxq);
		if (rc)
			return rc;
	}

	return 0;
}

/**
 * iwl4965_rx_replenish - Move all used packet from rx_used to rx_free
 *
 * When moving to rx_free an SKB is allocated for the slot.
 *
 * Also restock the Rx queue via iwl4965_rx_queue_restock.
 * This is called as a scheduled work item (except for during initialization)
 */
static void iwl4965_rx_allocate(struct iwl_priv *priv)
{
	struct iwl4965_rx_queue *rxq = &priv->rxq;
	struct list_head *element;
	struct iwl4965_rx_mem_buffer *rxb;
	unsigned long flags;
	spin_lock_irqsave(&rxq->lock, flags);
	while (!list_empty(&rxq->rx_used)) {
		element = rxq->rx_used.next;
		rxb = list_entry(element, struct iwl4965_rx_mem_buffer, list);

		/* Alloc a new receive buffer */
		rxb->skb =
		    alloc_skb(priv->hw_params.rx_buf_size,
				__GFP_NOWARN | GFP_ATOMIC);
		if (!rxb->skb) {
			if (net_ratelimit())
				printk(KERN_CRIT DRV_NAME
				       ": Can not allocate SKB buffers\n");
			/* We don't reschedule replenish work here -- we will
			 * call the restock method and if it still needs
			 * more buffers it will schedule replenish */
			break;
		}
		priv->alloc_rxb_skb++;
		list_del(element);

		/* Get physical address of RB/SKB */
		rxb->dma_addr =
		    pci_map_single(priv->pci_dev, rxb->skb->data,
			   priv->hw_params.rx_buf_size, PCI_DMA_FROMDEVICE);
		list_add_tail(&rxb->list, &rxq->rx_free);
		rxq->free_count++;
	}
	spin_unlock_irqrestore(&rxq->lock, flags);
}

/*
 * this should be called while priv->lock is locked
*/
static void __iwl4965_rx_replenish(void *data)
{
	struct iwl_priv *priv = data;

	iwl4965_rx_allocate(priv);
	iwl4965_rx_queue_restock(priv);
}


void iwl4965_rx_replenish(void *data)
{
	struct iwl_priv *priv = data;
	unsigned long flags;

	iwl4965_rx_allocate(priv);

	spin_lock_irqsave(&priv->lock, flags);
	iwl4965_rx_queue_restock(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
}

/* Assumes that the skb field of the buffers in 'pool' is kept accurate.
 * If an SKB has been detached, the POOL needs to have its SKB set to NULL
 * This free routine walks the list of POOL entries and if SKB is set to
 * non NULL it is unmapped and freed
 */
static void iwl4965_rx_queue_free(struct iwl_priv *priv, struct iwl4965_rx_queue *rxq)
{
	int i;
	for (i = 0; i < RX_QUEUE_SIZE + RX_FREE_BUFFERS; i++) {
		if (rxq->pool[i].skb != NULL) {
			pci_unmap_single(priv->pci_dev,
					 rxq->pool[i].dma_addr,
					 priv->hw_params.rx_buf_size,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb(rxq->pool[i].skb);
		}
	}

	pci_free_consistent(priv->pci_dev, 4 * RX_QUEUE_SIZE, rxq->bd,
			    rxq->dma_addr);
	rxq->bd = NULL;
}

int iwl4965_rx_queue_alloc(struct iwl_priv *priv)
{
	struct iwl4965_rx_queue *rxq = &priv->rxq;
	struct pci_dev *dev = priv->pci_dev;
	int i;

	spin_lock_init(&rxq->lock);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);

	/* Alloc the circular buffer of Read Buffer Descriptors (RBDs) */
	rxq->bd = pci_alloc_consistent(dev, 4 * RX_QUEUE_SIZE, &rxq->dma_addr);
	if (!rxq->bd)
		return -ENOMEM;

	/* Fill the rx_used queue with _all_ of the Rx buffers */
	for (i = 0; i < RX_FREE_BUFFERS + RX_QUEUE_SIZE; i++)
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->free_count = 0;
	rxq->need_update = 0;
	return 0;
}

void iwl4965_rx_queue_reset(struct iwl_priv *priv, struct iwl4965_rx_queue *rxq)
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
		if (rxq->pool[i].skb != NULL) {
			pci_unmap_single(priv->pci_dev,
					 rxq->pool[i].dma_addr,
					 priv->hw_params.rx_buf_size,
					 PCI_DMA_FROMDEVICE);
			priv->alloc_rxb_skb--;
			dev_kfree_skb(rxq->pool[i].skb);
			rxq->pool[i].skb = NULL;
		}
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);
	}

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->free_count = 0;
	spin_unlock_irqrestore(&rxq->lock, flags);
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
int iwl4965_calc_db_from_ratio(int sig_ratio)
{
	/* 1000:1 or higher just report as 60 dB */
	if (sig_ratio >= 1000)
		return 60;

	/* 100:1 or higher, divide by 10 and use table,
	 *   add 20 dB to make up for divide by 10 */
	if (sig_ratio >= 100)
		return (20 + (int)ratio2dB[sig_ratio/10]);

	/* We shouldn't see this */
	if (sig_ratio < 1)
		return 0;

	/* Use table for ratios 1:1 - 99:1 */
	return (int)ratio2dB[sig_ratio];
}

#define PERFECT_RSSI (-20) /* dBm */
#define WORST_RSSI (-95)   /* dBm */
#define RSSI_RANGE (PERFECT_RSSI - WORST_RSSI)

/* Calculate an indication of rx signal quality (a percentage, not dBm!).
 * See http://www.ces.clemson.edu/linux/signal_quality.shtml for info
 *   about formulas used below. */
int iwl4965_calc_sig_qual(int rssi_dbm, int noise_dbm)
{
	int sig_qual;
	int degradation = PERFECT_RSSI - rssi_dbm;

	/* If we get a noise measurement, use signal-to-noise ratio (SNR)
	 * as indicator; formula is (signal dbm - noise dbm).
	 * SNR at or above 40 is a great signal (100%).
	 * Below that, scale to fit SNR of 0 - 40 dB within 0 - 100% indicator.
	 * Weakest usable signal is usually 10 - 15 dB SNR. */
	if (noise_dbm) {
		if (rssi_dbm - noise_dbm >= 40)
			return 100;
		else if (rssi_dbm < noise_dbm)
			return 0;
		sig_qual = ((rssi_dbm - noise_dbm) * 5) / 2;

	/* Else use just the signal level.
	 * This formula is a least squares fit of data points collected and
	 *   compared with a reference system that had a percentage (%) display
	 *   for signal quality. */
	} else
		sig_qual = (100 * (RSSI_RANGE * RSSI_RANGE) - degradation *
			    (15 * RSSI_RANGE + 62 * degradation)) /
			   (RSSI_RANGE * RSSI_RANGE);

	if (sig_qual > 100)
		sig_qual = 100;
	else if (sig_qual < 1)
		sig_qual = 0;

	return sig_qual;
}

/**
 * iwl4965_rx_handle - Main entry function for receiving responses from uCode
 *
 * Uses the priv->rx_handlers callback function array to invoke
 * the appropriate handlers, including command responses,
 * frame-received notifications, and other notifications.
 */
static void iwl4965_rx_handle(struct iwl_priv *priv)
{
	struct iwl4965_rx_mem_buffer *rxb;
	struct iwl4965_rx_packet *pkt;
	struct iwl4965_rx_queue *rxq = &priv->rxq;
	u32 r, i;
	int reclaim;
	unsigned long flags;
	u8 fill_rx = 0;
	u32 count = 8;

	/* uCode's read index (stored in shared DRAM) indicates the last Rx
	 * buffer that the driver may process (last buffer filled by ucode). */
	r = iwl4965_hw_get_rx_read(priv);
	i = rxq->read;

	/* Rx interrupt, but nothing sent from uCode */
	if (i == r)
		IWL_DEBUG(IWL_DL_RX | IWL_DL_ISR, "r = %d, i = %d\n", r, i);

	if (iwl4965_rx_queue_space(rxq) > (RX_QUEUE_SIZE / 2))
		fill_rx = 1;

	while (i != r) {
		rxb = rxq->queue[i];

		/* If an RXB doesn't have a Rx queue slot associated with it,
		 * then a bug has been introduced in the queue refilling
		 * routines -- catch it here */
		BUG_ON(rxb == NULL);

		rxq->queue[i] = NULL;

		pci_dma_sync_single_for_cpu(priv->pci_dev, rxb->dma_addr,
					    priv->hw_params.rx_buf_size,
					    PCI_DMA_FROMDEVICE);
		pkt = (struct iwl4965_rx_packet *)rxb->skb->data;

		/* Reclaim a command buffer only if this packet is a response
		 *   to a (driver-originated) command.
		 * If the packet (e.g. Rx frame) originated from uCode,
		 *   there is no command buffer to reclaim.
		 * Ucode should set SEQ_RX_FRAME bit if ucode-originated,
		 *   but apparently a few don't get set; catch them here. */
		reclaim = !(pkt->hdr.sequence & SEQ_RX_FRAME) &&
			(pkt->hdr.cmd != REPLY_RX_PHY_CMD) &&
			(pkt->hdr.cmd != REPLY_RX) &&
			(pkt->hdr.cmd != REPLY_COMPRESSED_BA) &&
			(pkt->hdr.cmd != STATISTICS_NOTIFICATION) &&
			(pkt->hdr.cmd != REPLY_TX);

		/* Based on type of command response or notification,
		 *   handle those that need handling via function in
		 *   rx_handlers table.  See iwl4965_setup_rx_handlers() */
		if (priv->rx_handlers[pkt->hdr.cmd]) {
			IWL_DEBUG(IWL_DL_HOST_COMMAND | IWL_DL_RX | IWL_DL_ISR,
				"r = %d, i = %d, %s, 0x%02x\n", r, i,
				get_cmd_string(pkt->hdr.cmd), pkt->hdr.cmd);
			priv->rx_handlers[pkt->hdr.cmd] (priv, rxb);
		} else {
			/* No handling needed */
			IWL_DEBUG(IWL_DL_HOST_COMMAND | IWL_DL_RX | IWL_DL_ISR,
				"r %d i %d No handler needed for %s, 0x%02x\n",
				r, i, get_cmd_string(pkt->hdr.cmd),
				pkt->hdr.cmd);
		}

		if (reclaim) {
			/* Invoke any callbacks, transfer the skb to caller, and
			 * fire off the (possibly) blocking iwl_send_cmd()
			 * as we reclaim the driver command queue */
			if (rxb && rxb->skb)
				iwl4965_tx_cmd_complete(priv, rxb);
			else
				IWL_WARNING("Claim null rxb?\n");
		}

		/* For now we just don't re-use anything.  We can tweak this
		 * later to try and re-use notification packets and SKBs that
		 * fail to Rx correctly */
		if (rxb->skb != NULL) {
			priv->alloc_rxb_skb--;
			dev_kfree_skb_any(rxb->skb);
			rxb->skb = NULL;
		}

		pci_unmap_single(priv->pci_dev, rxb->dma_addr,
				 priv->hw_params.rx_buf_size,
				 PCI_DMA_FROMDEVICE);
		spin_lock_irqsave(&rxq->lock, flags);
		list_add_tail(&rxb->list, &priv->rxq.rx_used);
		spin_unlock_irqrestore(&rxq->lock, flags);
		i = (i + 1) & RX_QUEUE_MASK;
		/* If there are a lot of unused frames,
		 * restock the Rx queue so ucode wont assert. */
		if (fill_rx) {
			count++;
			if (count >= 8) {
				priv->rxq.read = i;
				__iwl4965_rx_replenish(priv);
				count = 0;
			}
		}
	}

	/* Backtrack one entry */
	priv->rxq.read = i;
	iwl4965_rx_queue_restock(priv);
}

/**
 * iwl4965_tx_queue_update_write_ptr - Send new write index to hardware
 */
static int iwl4965_tx_queue_update_write_ptr(struct iwl_priv *priv,
				  struct iwl4965_tx_queue *txq)
{
	u32 reg = 0;
	int rc = 0;
	int txq_id = txq->q.id;

	if (txq->need_update == 0)
		return rc;

	/* if we're trying to save power */
	if (test_bit(STATUS_POWER_PMI, &priv->status)) {
		/* wake up nic if it's powered down ...
		 * uCode will wake up, and interrupt us again, so next
		 * time we'll skip this part. */
		reg = iwl_read32(priv, CSR_UCODE_DRV_GP1);

		if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
			IWL_DEBUG_INFO("Requesting wakeup, GP1 = 0x%x\n", reg);
			iwl_set_bit(priv, CSR_GP_CNTRL,
				    CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
			return rc;
		}

		/* restore this queue's parameters in nic hardware. */
		rc = iwl_grab_nic_access(priv);
		if (rc)
			return rc;
		iwl_write_direct32(priv, HBUS_TARG_WRPTR,
				     txq->q.write_ptr | (txq_id << 8));
		iwl_release_nic_access(priv);

	/* else not in power-save mode, uCode will never sleep when we're
	 * trying to tx (during RFKILL, we're not trying to tx). */
	} else
		iwl_write32(priv, HBUS_TARG_WRPTR,
			    txq->q.write_ptr | (txq_id << 8));

	txq->need_update = 0;

	return rc;
}

#ifdef CONFIG_IWLWIFI_DEBUG
static void iwl4965_print_rx_config_cmd(struct iwl4965_rxon_cmd *rxon)
{
	DECLARE_MAC_BUF(mac);

	IWL_DEBUG_RADIO("RX CONFIG:\n");
	iwl_print_hex_dump(IWL_DL_RADIO, (u8 *) rxon, sizeof(*rxon));
	IWL_DEBUG_RADIO("u16 channel: 0x%x\n", le16_to_cpu(rxon->channel));
	IWL_DEBUG_RADIO("u32 flags: 0x%08X\n", le32_to_cpu(rxon->flags));
	IWL_DEBUG_RADIO("u32 filter_flags: 0x%08x\n",
			le32_to_cpu(rxon->filter_flags));
	IWL_DEBUG_RADIO("u8 dev_type: 0x%x\n", rxon->dev_type);
	IWL_DEBUG_RADIO("u8 ofdm_basic_rates: 0x%02x\n",
			rxon->ofdm_basic_rates);
	IWL_DEBUG_RADIO("u8 cck_basic_rates: 0x%02x\n", rxon->cck_basic_rates);
	IWL_DEBUG_RADIO("u8[6] node_addr: %s\n",
			print_mac(mac, rxon->node_addr));
	IWL_DEBUG_RADIO("u8[6] bssid_addr: %s\n",
			print_mac(mac, rxon->bssid_addr));
	IWL_DEBUG_RADIO("u16 assoc_id: 0x%x\n", le16_to_cpu(rxon->assoc_id));
}
#endif

static void iwl4965_enable_interrupts(struct iwl_priv *priv)
{
	IWL_DEBUG_ISR("Enabling interrupts\n");
	set_bit(STATUS_INT_ENABLED, &priv->status);
	iwl_write32(priv, CSR_INT_MASK, CSR_INI_SET_MASK);
}

/* call this function to flush any scheduled tasklet */
static inline void iwl_synchronize_irq(struct iwl_priv *priv)
{
	/* wait to make sure we flush pedding tasklet*/
	synchronize_irq(priv->pci_dev->irq);
	tasklet_kill(&priv->irq_tasklet);
}

static inline void iwl4965_disable_interrupts(struct iwl_priv *priv)
{
	clear_bit(STATUS_INT_ENABLED, &priv->status);

	/* disable interrupts from uCode/NIC to host */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);

	/* acknowledge/clear/reset any interrupts still pending
	 * from uCode or flow handler (Rx/Tx DMA) */
	iwl_write32(priv, CSR_INT, 0xffffffff);
	iwl_write32(priv, CSR_FH_INT_STATUS, 0xffffffff);
	IWL_DEBUG_ISR("Disabled interrupts\n");
}

static const char *desc_lookup(int i)
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

static void iwl4965_dump_nic_error_log(struct iwl_priv *priv)
{
	u32 data2, line;
	u32 desc, time, count, base, data1;
	u32 blink1, blink2, ilink1, ilink2;
	int rc;

	base = le32_to_cpu(priv->card_alive.error_event_table_ptr);

	if (!priv->cfg->ops->lib->is_valid_rtc_data_addr(base)) {
		IWL_ERROR("Not valid error log pointer 0x%08X\n", base);
		return;
	}

	rc = iwl_grab_nic_access(priv);
	if (rc) {
		IWL_WARNING("Can not read from adapter at this time.\n");
		return;
	}

	count = iwl_read_targ_mem(priv, base);

	if (ERROR_START_OFFSET <= count * ERROR_ELEM_SIZE) {
		IWL_ERROR("Start IWL Error Log Dump:\n");
		IWL_ERROR("Status: 0x%08lX, count: %d\n", priv->status, count);
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

	IWL_ERROR("Desc               Time       "
		  "data1      data2      line\n");
	IWL_ERROR("%-13s (#%d) %010u 0x%08X 0x%08X %u\n",
		  desc_lookup(desc), desc, time, data1, data2, line);
	IWL_ERROR("blink1  blink2  ilink1  ilink2\n");
	IWL_ERROR("0x%05X 0x%05X 0x%05X 0x%05X\n", blink1, blink2,
		  ilink1, ilink2);

	iwl_release_nic_access(priv);
}

#define EVENT_START_OFFSET  (4 * sizeof(u32))

/**
 * iwl4965_print_event_log - Dump error event log to syslog
 *
 * NOTE: Must be called with iwl_grab_nic_access() already obtained!
 */
static void iwl4965_print_event_log(struct iwl_priv *priv, u32 start_idx,
				u32 num_events, u32 mode)
{
	u32 i;
	u32 base;       /* SRAM byte address of event log header */
	u32 event_size;	/* 2 u32s, or 3 u32s if timestamp recorded */
	u32 ptr;        /* SRAM byte address of log data */
	u32 ev, time, data; /* event log data */

	if (num_events == 0)
		return;

	base = le32_to_cpu(priv->card_alive.log_event_table_ptr);

	if (mode == 0)
		event_size = 2 * sizeof(u32);
	else
		event_size = 3 * sizeof(u32);

	ptr = base + EVENT_START_OFFSET + (start_idx * event_size);

	/* "time" is actually "data" for mode 0 (no timestamp).
	 * place event id # at far right for easier visual parsing. */
	for (i = 0; i < num_events; i++) {
		ev = iwl_read_targ_mem(priv, ptr);
		ptr += sizeof(u32);
		time = iwl_read_targ_mem(priv, ptr);
		ptr += sizeof(u32);
		if (mode == 0)
			IWL_ERROR("0x%08x\t%04u\n", time, ev); /* data, ev */
		else {
			data = iwl_read_targ_mem(priv, ptr);
			ptr += sizeof(u32);
			IWL_ERROR("%010u\t0x%08x\t%04u\n", time, data, ev);
		}
	}
}

static void iwl4965_dump_nic_event_log(struct iwl_priv *priv)
{
	int rc;
	u32 base;       /* SRAM byte address of event log header */
	u32 capacity;   /* event log capacity in # entries */
	u32 mode;       /* 0 - no timestamp, 1 - timestamp recorded */
	u32 num_wraps;  /* # times uCode wrapped to top of log */
	u32 next_entry; /* index of next entry to be written by uCode */
	u32 size;       /* # entries that we'll print */

	base = le32_to_cpu(priv->card_alive.log_event_table_ptr);
	if (!priv->cfg->ops->lib->is_valid_rtc_data_addr(base)) {
		IWL_ERROR("Invalid event log pointer 0x%08X\n", base);
		return;
	}

	rc = iwl_grab_nic_access(priv);
	if (rc) {
		IWL_WARNING("Can not read from adapter at this time.\n");
		return;
	}

	/* event log header */
	capacity = iwl_read_targ_mem(priv, base);
	mode = iwl_read_targ_mem(priv, base + (1 * sizeof(u32)));
	num_wraps = iwl_read_targ_mem(priv, base + (2 * sizeof(u32)));
	next_entry = iwl_read_targ_mem(priv, base + (3 * sizeof(u32)));

	size = num_wraps ? capacity : next_entry;

	/* bail out if nothing in log */
	if (size == 0) {
		IWL_ERROR("Start IWL Event Log Dump: nothing in log\n");
		iwl_release_nic_access(priv);
		return;
	}

	IWL_ERROR("Start IWL Event Log Dump: display count %d, wraps %d\n",
		  size, num_wraps);

	/* if uCode has wrapped back to top of log, start at the oldest entry,
	 * i.e the next one that uCode would fill. */
	if (num_wraps)
		iwl4965_print_event_log(priv, next_entry,
				    capacity - next_entry, mode);

	/* (then/else) start at top of log */
	iwl4965_print_event_log(priv, 0, next_entry, mode);

	iwl_release_nic_access(priv);
}

/**
 * iwl4965_irq_handle_error - called for HW or SW error interrupt from card
 */
static void iwl4965_irq_handle_error(struct iwl_priv *priv)
{
	/* Set the FW error flag -- cleared on iwl4965_down */
	set_bit(STATUS_FW_ERROR, &priv->status);

	/* Cancel currently queued command. */
	clear_bit(STATUS_HCMD_ACTIVE, &priv->status);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_debug_level & IWL_DL_FW_ERRORS) {
		iwl4965_dump_nic_error_log(priv);
		iwl4965_dump_nic_event_log(priv);
		iwl4965_print_rx_config_cmd(&priv->staging_rxon);
	}
#endif

	wake_up_interruptible(&priv->wait_command_queue);

	/* Keep the restart process from trying to send host
	 * commands by clearing the INIT status bit */
	clear_bit(STATUS_READY, &priv->status);

	if (!test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_DEBUG(IWL_DL_INFO | IWL_DL_FW_ERRORS,
			  "Restarting adapter due to uCode error.\n");

		if (iwl_is_associated(priv)) {
			memcpy(&priv->recovery_rxon, &priv->active_rxon,
			       sizeof(priv->recovery_rxon));
			priv->error_recovering = 1;
		}
		queue_work(priv->workqueue, &priv->restart);
	}
}

static void iwl4965_error_recovery(struct iwl_priv *priv)
{
	unsigned long flags;

	memcpy(&priv->staging_rxon, &priv->recovery_rxon,
	       sizeof(priv->staging_rxon));
	priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwl4965_commit_rxon(priv);

	iwl4965_rxon_add_station(priv, priv->bssid, 1);

	spin_lock_irqsave(&priv->lock, flags);
	priv->assoc_id = le16_to_cpu(priv->staging_rxon.assoc_id);
	priv->error_recovering = 0;
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void iwl4965_irq_tasklet(struct iwl_priv *priv)
{
	u32 inta, handled = 0;
	u32 inta_fh;
	unsigned long flags;
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
	if (iwl_debug_level & IWL_DL_ISR) {
		/* just for debug */
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		IWL_DEBUG_ISR("inta 0x%08x, enabled 0x%08x, fh 0x%08x\n",
			      inta, inta_mask, inta_fh);
	}
#endif

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
		IWL_ERROR("Microcode HW error detected.  Restarting.\n");

		/* Tell the device to stop sending interrupts */
		iwl4965_disable_interrupts(priv);

		iwl4965_irq_handle_error(priv);

		handled |= CSR_INT_BIT_HW_ERR;

		spin_unlock_irqrestore(&priv->lock, flags);

		return;
	}

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_debug_level & (IWL_DL_ISR)) {
		/* NIC fires this, but we don't use it, redundant with WAKEUP */
		if (inta & CSR_INT_BIT_SCD)
			IWL_DEBUG_ISR("Scheduler finished to transmit "
				      "the frame/frames.\n");

		/* Alive notification via Rx interrupt will do the real work */
		if (inta & CSR_INT_BIT_ALIVE)
			IWL_DEBUG_ISR("Alive interrupt\n");
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

		IWL_DEBUG(IWL_DL_INFO | IWL_DL_RF_KILL | IWL_DL_ISR,
				"RF_KILL bit toggled to %s.\n",
				hw_rf_kill ? "disable radio":"enable radio");

		/* Queue restart only if RF_KILL switch was set to "kill"
		 *   when we loaded driver, and is now set to "enable".
		 * After we're Alive, RF_KILL gets handled by
		 *   iwl4965_rx_card_state_notif() */
		if (!hw_rf_kill && !test_bit(STATUS_ALIVE, &priv->status)) {
			clear_bit(STATUS_RF_KILL_HW, &priv->status);
			queue_work(priv->workqueue, &priv->restart);
		}

		handled |= CSR_INT_BIT_RF_KILL;
	}

	/* Chip got too hot and stopped itself */
	if (inta & CSR_INT_BIT_CT_KILL) {
		IWL_ERROR("Microcode CT kill error detected.\n");
		handled |= CSR_INT_BIT_CT_KILL;
	}

	/* Error detected by uCode */
	if (inta & CSR_INT_BIT_SW_ERR) {
		IWL_ERROR("Microcode SW error detected.  Restarting 0x%X.\n",
			  inta);
		iwl4965_irq_handle_error(priv);
		handled |= CSR_INT_BIT_SW_ERR;
	}

	/* uCode wakes up after power-down sleep */
	if (inta & CSR_INT_BIT_WAKEUP) {
		IWL_DEBUG_ISR("Wakeup interrupt\n");
		iwl4965_rx_queue_update_write_ptr(priv, &priv->rxq);
		iwl4965_tx_queue_update_write_ptr(priv, &priv->txq[0]);
		iwl4965_tx_queue_update_write_ptr(priv, &priv->txq[1]);
		iwl4965_tx_queue_update_write_ptr(priv, &priv->txq[2]);
		iwl4965_tx_queue_update_write_ptr(priv, &priv->txq[3]);
		iwl4965_tx_queue_update_write_ptr(priv, &priv->txq[4]);
		iwl4965_tx_queue_update_write_ptr(priv, &priv->txq[5]);

		handled |= CSR_INT_BIT_WAKEUP;
	}

	/* All uCode command responses, including Tx command responses,
	 * Rx "responses" (frame-received notification), and other
	 * notifications from uCode come through here*/
	if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX)) {
		iwl4965_rx_handle(priv);
		handled |= (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX);
	}

	if (inta & CSR_INT_BIT_FH_TX) {
		IWL_DEBUG_ISR("Tx interrupt\n");
		handled |= CSR_INT_BIT_FH_TX;
	}

	if (inta & ~handled)
		IWL_ERROR("Unhandled INTA bits 0x%08x\n", inta & ~handled);

	if (inta & ~CSR_INI_SET_MASK) {
		IWL_WARNING("Disabled INTA bits 0x%08x were pending\n",
			 inta & ~CSR_INI_SET_MASK);
		IWL_WARNING("   with FH_INT = 0x%08x\n", inta_fh);
	}

	/* Re-enable all interrupts */
	/* only Re-enable if diabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl4965_enable_interrupts(priv);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_debug_level & (IWL_DL_ISR)) {
		inta = iwl_read32(priv, CSR_INT);
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);
		IWL_DEBUG_ISR("End inta 0x%08x, enabled 0x%08x, fh 0x%08x, "
			"flags 0x%08lx\n", inta, inta_mask, inta_fh, flags);
	}
#endif
	spin_unlock_irqrestore(&priv->lock, flags);
}

static irqreturn_t iwl4965_isr(int irq, void *data)
{
	struct iwl_priv *priv = data;
	u32 inta, inta_mask;
	u32 inta_fh;
	if (!priv)
		return IRQ_NONE;

	spin_lock(&priv->lock);

	/* Disable (but don't clear!) interrupts here to avoid
	 *    back-to-back ISRs and sporadic interrupts from our NIC.
	 * If we have something to service, the tasklet will re-enable ints.
	 * If we *don't* have something, we'll re-enable before leaving here. */
	inta_mask = iwl_read32(priv, CSR_INT_MASK);  /* just for debug */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);

	/* Discover which interrupts are active/pending */
	inta = iwl_read32(priv, CSR_INT);
	inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);

	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	if (!inta && !inta_fh) {
		IWL_DEBUG_ISR("Ignore interrupt, inta == 0, inta_fh == 0\n");
		goto none;
	}

	if ((inta == 0xFFFFFFFF) || ((inta & 0xFFFFFFF0) == 0xa5a5a5a0)) {
		/* Hardware disappeared. It might have already raised
		 * an interrupt */
		IWL_WARNING("HARDWARE GONE?? INTA == 0x%080x\n", inta);
		goto unplugged;
	}

	IWL_DEBUG_ISR("ISR inta 0x%08x, enabled 0x%08x, fh 0x%08x\n",
		      inta, inta_mask, inta_fh);

	inta &= ~CSR_INT_BIT_SCD;

	/* iwl4965_irq_tasklet() will service interrupts and re-enable them */
	if (likely(inta || inta_fh))
		tasklet_schedule(&priv->irq_tasklet);

 unplugged:
	spin_unlock(&priv->lock);
	return IRQ_HANDLED;

 none:
	/* re-enable interrupts here since we don't have anything to service. */
	/* only Re-enable if diabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl4965_enable_interrupts(priv);
	spin_unlock(&priv->lock);
	return IRQ_NONE;
}

/* For active scan, listen ACTIVE_DWELL_TIME (msec) on each channel after
 * sending probe req.  This should be set long enough to hear probe responses
 * from more than one AP.  */
#define IWL_ACTIVE_DWELL_TIME_24    (20)	/* all times in msec */
#define IWL_ACTIVE_DWELL_TIME_52    (10)

/* For faster active scanning, scan will move to the next channel if fewer than
 * PLCP_QUIET_THRESH packets are heard on this channel within
 * ACTIVE_QUIET_TIME after sending probe request.  This shortens the dwell
 * time if it's a quiet channel (nothing responded to our probe, and there's
 * no other traffic).
 * Disable "quiet" feature by setting PLCP_QUIET_THRESH to 0. */
#define IWL_PLCP_QUIET_THRESH       __constant_cpu_to_le16(1)	/* packets */
#define IWL_ACTIVE_QUIET_TIME       __constant_cpu_to_le16(5)	/* msec */

/* For passive scan, listen PASSIVE_DWELL_TIME (msec) on each channel.
 * Must be set longer than active dwell time.
 * For the most reliable scan, set > AP beacon interval (typically 100msec). */
#define IWL_PASSIVE_DWELL_TIME_24   (20)	/* all times in msec */
#define IWL_PASSIVE_DWELL_TIME_52   (10)
#define IWL_PASSIVE_DWELL_BASE      (100)
#define IWL_CHANNEL_TUNE_TIME       5

static inline u16 iwl4965_get_active_dwell_time(struct iwl_priv *priv,
						enum ieee80211_band band)
{
	if (band == IEEE80211_BAND_5GHZ)
		return IWL_ACTIVE_DWELL_TIME_52;
	else
		return IWL_ACTIVE_DWELL_TIME_24;
}

static u16 iwl4965_get_passive_dwell_time(struct iwl_priv *priv,
					  enum ieee80211_band band)
{
	u16 active = iwl4965_get_active_dwell_time(priv, band);
	u16 passive = (band != IEEE80211_BAND_5GHZ) ?
	    IWL_PASSIVE_DWELL_BASE + IWL_PASSIVE_DWELL_TIME_24 :
	    IWL_PASSIVE_DWELL_BASE + IWL_PASSIVE_DWELL_TIME_52;

	if (iwl_is_associated(priv)) {
		/* If we're associated, we clamp the maximum passive
		 * dwell time to be 98% of the beacon interval (minus
		 * 2 * channel tune time) */
		passive = priv->beacon_int;
		if ((passive > IWL_PASSIVE_DWELL_BASE) || !passive)
			passive = IWL_PASSIVE_DWELL_BASE;
		passive = (passive * 98) / 100 - IWL_CHANNEL_TUNE_TIME * 2;
	}

	if (passive <= active)
		passive = active + 1;

	return passive;
}

static int iwl4965_get_channels_for_scan(struct iwl_priv *priv,
					 enum ieee80211_band band,
				     u8 is_active, u8 direct_mask,
				     struct iwl4965_scan_channel *scan_ch)
{
	const struct ieee80211_channel *channels = NULL;
	const struct ieee80211_supported_band *sband;
	const struct iwl_channel_info *ch_info;
	u16 passive_dwell = 0;
	u16 active_dwell = 0;
	int added, i;

	sband = iwl4965_get_hw_mode(priv, band);
	if (!sband)
		return 0;

	channels = sband->channels;

	active_dwell = iwl4965_get_active_dwell_time(priv, band);
	passive_dwell = iwl4965_get_passive_dwell_time(priv, band);

	for (i = 0, added = 0; i < sband->n_channels; i++) {
		if (channels[i].flags & IEEE80211_CHAN_DISABLED)
			continue;

		scan_ch->channel = ieee80211_frequency_to_channel(channels[i].center_freq);

		ch_info = iwl_get_channel_info(priv, band,
					 scan_ch->channel);
		if (!is_channel_valid(ch_info)) {
			IWL_DEBUG_SCAN("Channel %d is INVALID for this SKU.\n",
				       scan_ch->channel);
			continue;
		}

		if (!is_active || is_channel_passive(ch_info) ||
		    (channels[i].flags & IEEE80211_CHAN_PASSIVE_SCAN))
			scan_ch->type = 0;	/* passive */
		else
			scan_ch->type = 1;	/* active */

		if (scan_ch->type & 1)
			scan_ch->type |= (direct_mask << 1);

		if (is_channel_narrow(ch_info))
			scan_ch->type |= (1 << 7);

		scan_ch->active_dwell = cpu_to_le16(active_dwell);
		scan_ch->passive_dwell = cpu_to_le16(passive_dwell);

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

		IWL_DEBUG_SCAN("Scanning %d [%s %d]\n",
			       scan_ch->channel,
			       (scan_ch->type & 1) ? "ACTIVE" : "PASSIVE",
			       (scan_ch->type & 1) ?
			       active_dwell : passive_dwell);

		scan_ch++;
		added++;
	}

	IWL_DEBUG_SCAN("total channels to scan %d \n", added);
	return added;
}

static void iwl4965_init_hw_rates(struct iwl_priv *priv,
			      struct ieee80211_rate *rates)
{
	int i;

	for (i = 0; i < IWL_RATE_COUNT; i++) {
		rates[i].bitrate = iwl4965_rates[i].ieee * 5;
		rates[i].hw_value = i; /* Rate scaling will work on indexes */
		rates[i].hw_value_short = i;
		rates[i].flags = 0;
		if ((i > IWL_LAST_OFDM_RATE) || (i < IWL_FIRST_OFDM_RATE)) {
			/*
			 * If CCK != 1M then set short preamble rate flag.
			 */
			rates[i].flags |=
				(iwl4965_rates[i].plcp == IWL_RATE_1M_PLCP) ?
					0 : IEEE80211_RATE_SHORT_PREAMBLE;
		}
	}
}

/**
 * iwl4965_init_geos - Initialize mac80211's geo/channel info based from eeprom
 */
int iwl4965_init_geos(struct iwl_priv *priv)
{
	struct iwl_channel_info *ch;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *channels;
	struct ieee80211_channel *geo_ch;
	struct ieee80211_rate *rates;
	int i = 0;

	if (priv->bands[IEEE80211_BAND_2GHZ].n_bitrates ||
	    priv->bands[IEEE80211_BAND_5GHZ].n_bitrates) {
		IWL_DEBUG_INFO("Geography modes already initialized.\n");
		set_bit(STATUS_GEO_CONFIGURED, &priv->status);
		return 0;
	}

	channels = kzalloc(sizeof(struct ieee80211_channel) *
			   priv->channel_count, GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	rates = kzalloc((sizeof(struct ieee80211_rate) * (IWL_RATE_COUNT + 1)),
			GFP_KERNEL);
	if (!rates) {
		kfree(channels);
		return -ENOMEM;
	}

	/* 5.2GHz channels start after the 2.4GHz channels */
	sband = &priv->bands[IEEE80211_BAND_5GHZ];
	sband->channels = &channels[ARRAY_SIZE(iwl_eeprom_band_1)];
	/* just OFDM */
	sband->bitrates = &rates[IWL_FIRST_OFDM_RATE];
	sband->n_bitrates = IWL_RATE_COUNT - IWL_FIRST_OFDM_RATE;

	iwl4965_init_ht_hw_capab(priv, &sband->ht_info, IEEE80211_BAND_5GHZ);

	sband = &priv->bands[IEEE80211_BAND_2GHZ];
	sband->channels = channels;
	/* OFDM & CCK */
	sband->bitrates = rates;
	sband->n_bitrates = IWL_RATE_COUNT;

	iwl4965_init_ht_hw_capab(priv, &sband->ht_info, IEEE80211_BAND_2GHZ);

	priv->ieee_channels = channels;
	priv->ieee_rates = rates;

	iwl4965_init_hw_rates(priv, rates);

	for (i = 0;  i < priv->channel_count; i++) {
		ch = &priv->channel_info[i];

		/* FIXME: might be removed if scan is OK */
		if (!is_channel_valid(ch))
			continue;

		if (is_channel_a_band(ch))
			sband =  &priv->bands[IEEE80211_BAND_5GHZ];
		else
			sband =  &priv->bands[IEEE80211_BAND_2GHZ];

		geo_ch = &sband->channels[sband->n_channels++];

		geo_ch->center_freq = ieee80211_channel_to_frequency(ch->channel);
		geo_ch->max_power = ch->max_power_avg;
		geo_ch->max_antenna_gain = 0xff;
		geo_ch->hw_value = ch->channel;

		if (is_channel_valid(ch)) {
			if (!(ch->flags & EEPROM_CHANNEL_IBSS))
				geo_ch->flags |= IEEE80211_CHAN_NO_IBSS;

			if (!(ch->flags & EEPROM_CHANNEL_ACTIVE))
				geo_ch->flags |= IEEE80211_CHAN_PASSIVE_SCAN;

			if (ch->flags & EEPROM_CHANNEL_RADAR)
				geo_ch->flags |= IEEE80211_CHAN_RADAR;

			if (ch->max_power_avg > priv->max_channel_txpower_limit)
				priv->max_channel_txpower_limit =
				    ch->max_power_avg;
		} else {
			geo_ch->flags |= IEEE80211_CHAN_DISABLED;
		}

		/* Save flags for reg domain usage */
		geo_ch->orig_flags = geo_ch->flags;

		IWL_DEBUG_INFO("Channel %d Freq=%d[%sGHz] %s flag=0%X\n",
				ch->channel, geo_ch->center_freq,
				is_channel_a_band(ch) ?  "5.2" : "2.4",
				geo_ch->flags & IEEE80211_CHAN_DISABLED ?
				"restricted" : "valid",
				 geo_ch->flags);
	}

	if ((priv->bands[IEEE80211_BAND_5GHZ].n_channels == 0) &&
	     priv->cfg->sku & IWL_SKU_A) {
		printk(KERN_INFO DRV_NAME
		       ": Incorrectly detected BG card as ABG.  Please send "
		       "your PCI ID 0x%04X:0x%04X to maintainer.\n",
		       priv->pci_dev->device, priv->pci_dev->subsystem_device);
		priv->cfg->sku &= ~IWL_SKU_A;
	}

	printk(KERN_INFO DRV_NAME
	       ": Tunable channels: %d 802.11bg, %d 802.11a channels\n",
	       priv->bands[IEEE80211_BAND_2GHZ].n_channels,
	       priv->bands[IEEE80211_BAND_5GHZ].n_channels);

	if (priv->bands[IEEE80211_BAND_2GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&priv->bands[IEEE80211_BAND_2GHZ];
	if (priv->bands[IEEE80211_BAND_5GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&priv->bands[IEEE80211_BAND_5GHZ];

	set_bit(STATUS_GEO_CONFIGURED, &priv->status);

	return 0;
}

/*
 * iwl4965_free_geos - undo allocations in iwl4965_init_geos
 */
void iwl4965_free_geos(struct iwl_priv *priv)
{
	kfree(priv->ieee_channels);
	kfree(priv->ieee_rates);
	clear_bit(STATUS_GEO_CONFIGURED, &priv->status);
}

/******************************************************************************
 *
 * uCode download functions
 *
 ******************************************************************************/

static void iwl4965_dealloc_ucode_pci(struct iwl_priv *priv)
{
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_code);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_data);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_data_backup);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_init);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_init_data);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_boot);
}

/**
 * iwl4965_verify_inst_full - verify runtime uCode image in card vs. host,
 *     looking at all data.
 */
static int iwl4965_verify_inst_full(struct iwl_priv *priv, __le32 *image,
				 u32 len)
{
	u32 val;
	u32 save_len = len;
	int rc = 0;
	u32 errcnt;

	IWL_DEBUG_INFO("ucode inst image size is %u\n", len);

	rc = iwl_grab_nic_access(priv);
	if (rc)
		return rc;

	iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR, RTC_INST_LOWER_BOUND);

	errcnt = 0;
	for (; len > 0; len -= sizeof(u32), image++) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		val = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			IWL_ERROR("uCode INST section is invalid at "
				  "offset 0x%x, is 0x%x, s/b 0x%x\n",
				  save_len - len, val, le32_to_cpu(*image));
			rc = -EIO;
			errcnt++;
			if (errcnt >= 20)
				break;
		}
	}

	iwl_release_nic_access(priv);

	if (!errcnt)
		IWL_DEBUG_INFO
		    ("ucode image in INSTRUCTION memory is good\n");

	return rc;
}


/**
 * iwl4965_verify_inst_sparse - verify runtime uCode image in card vs. host,
 *   using sample data 100 bytes apart.  If these sample points are good,
 *   it's a pretty good bet that everything between them is good, too.
 */
static int iwl4965_verify_inst_sparse(struct iwl_priv *priv, __le32 *image, u32 len)
{
	u32 val;
	int rc = 0;
	u32 errcnt = 0;
	u32 i;

	IWL_DEBUG_INFO("ucode inst image size is %u\n", len);

	rc = iwl_grab_nic_access(priv);
	if (rc)
		return rc;

	for (i = 0; i < len; i += 100, image += 100/sizeof(u32)) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR,
			i + RTC_INST_LOWER_BOUND);
		val = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
#if 0 /* Enable this if you want to see details */
			IWL_ERROR("uCode INST section is invalid at "
				  "offset 0x%x, is 0x%x, s/b 0x%x\n",
				  i, val, *image);
#endif
			rc = -EIO;
			errcnt++;
			if (errcnt >= 3)
				break;
		}
	}

	iwl_release_nic_access(priv);

	return rc;
}


/**
 * iwl4965_verify_ucode - determine which instruction image is in SRAM,
 *    and verify its contents
 */
static int iwl4965_verify_ucode(struct iwl_priv *priv)
{
	__le32 *image;
	u32 len;
	int rc = 0;

	/* Try bootstrap */
	image = (__le32 *)priv->ucode_boot.v_addr;
	len = priv->ucode_boot.len;
	rc = iwl4965_verify_inst_sparse(priv, image, len);
	if (rc == 0) {
		IWL_DEBUG_INFO("Bootstrap uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try initialize */
	image = (__le32 *)priv->ucode_init.v_addr;
	len = priv->ucode_init.len;
	rc = iwl4965_verify_inst_sparse(priv, image, len);
	if (rc == 0) {
		IWL_DEBUG_INFO("Initialize uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try runtime/protocol */
	image = (__le32 *)priv->ucode_code.v_addr;
	len = priv->ucode_code.len;
	rc = iwl4965_verify_inst_sparse(priv, image, len);
	if (rc == 0) {
		IWL_DEBUG_INFO("Runtime uCode is good in inst SRAM\n");
		return 0;
	}

	IWL_ERROR("NO VALID UCODE IMAGE IN INSTRUCTION SRAM!!\n");

	/* Since nothing seems to match, show first several data entries in
	 * instruction SRAM, so maybe visual inspection will give a clue.
	 * Selection of bootstrap image (vs. other images) is arbitrary. */
	image = (__le32 *)priv->ucode_boot.v_addr;
	len = priv->ucode_boot.len;
	rc = iwl4965_verify_inst_full(priv, image, len);

	return rc;
}

static void iwl4965_nic_start(struct iwl_priv *priv)
{
	/* Remove all resets to allow NIC to operate */
	iwl_write32(priv, CSR_RESET, 0);
}


/**
 * iwl4965_read_ucode - Read uCode images from disk file.
 *
 * Copy into buffers for card to fetch via bus-mastering
 */
static int iwl4965_read_ucode(struct iwl_priv *priv)
{
	struct iwl4965_ucode *ucode;
	int ret;
	const struct firmware *ucode_raw;
	const char *name = priv->cfg->fw_name;
	u8 *src;
	size_t len;
	u32 ver, inst_size, data_size, init_size, init_data_size, boot_size;

	/* Ask kernel firmware_class module to get the boot firmware off disk.
	 * request_firmware() is synchronous, file is in memory on return. */
	ret = request_firmware(&ucode_raw, name, &priv->pci_dev->dev);
	if (ret < 0) {
		IWL_ERROR("%s firmware file req failed: Reason %d\n",
					name, ret);
		goto error;
	}

	IWL_DEBUG_INFO("Got firmware '%s' file (%zd bytes) from disk\n",
		       name, ucode_raw->size);

	/* Make sure that we got at least our header! */
	if (ucode_raw->size < sizeof(*ucode)) {
		IWL_ERROR("File size way too small!\n");
		ret = -EINVAL;
		goto err_release;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (void *)ucode_raw->data;

	ver = le32_to_cpu(ucode->ver);
	inst_size = le32_to_cpu(ucode->inst_size);
	data_size = le32_to_cpu(ucode->data_size);
	init_size = le32_to_cpu(ucode->init_size);
	init_data_size = le32_to_cpu(ucode->init_data_size);
	boot_size = le32_to_cpu(ucode->boot_size);

	IWL_DEBUG_INFO("f/w package hdr ucode version = 0x%x\n", ver);
	IWL_DEBUG_INFO("f/w package hdr runtime inst size = %u\n",
		       inst_size);
	IWL_DEBUG_INFO("f/w package hdr runtime data size = %u\n",
		       data_size);
	IWL_DEBUG_INFO("f/w package hdr init inst size = %u\n",
		       init_size);
	IWL_DEBUG_INFO("f/w package hdr init data size = %u\n",
		       init_data_size);
	IWL_DEBUG_INFO("f/w package hdr boot inst size = %u\n",
		       boot_size);

	/* Verify size of file vs. image size info in file's header */
	if (ucode_raw->size < sizeof(*ucode) +
		inst_size + data_size + init_size +
		init_data_size + boot_size) {

		IWL_DEBUG_INFO("uCode file size %d too small\n",
			       (int)ucode_raw->size);
		ret = -EINVAL;
		goto err_release;
	}

	/* Verify that uCode images will fit in card's SRAM */
	if (inst_size > IWL_MAX_INST_SIZE) {
		IWL_DEBUG_INFO("uCode instr len %d too large to fit in\n",
			       inst_size);
		ret = -EINVAL;
		goto err_release;
	}

	if (data_size > IWL_MAX_DATA_SIZE) {
		IWL_DEBUG_INFO("uCode data len %d too large to fit in\n",
				data_size);
		ret = -EINVAL;
		goto err_release;
	}
	if (init_size > IWL_MAX_INST_SIZE) {
		IWL_DEBUG_INFO
		    ("uCode init instr len %d too large to fit in\n",
		      init_size);
		ret = -EINVAL;
		goto err_release;
	}
	if (init_data_size > IWL_MAX_DATA_SIZE) {
		IWL_DEBUG_INFO
		    ("uCode init data len %d too large to fit in\n",
		      init_data_size);
		ret = -EINVAL;
		goto err_release;
	}
	if (boot_size > IWL_MAX_BSM_SIZE) {
		IWL_DEBUG_INFO
		    ("uCode boot instr len %d too large to fit in\n",
		      boot_size);
		ret = -EINVAL;
		goto err_release;
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
	src = &ucode->data[0];
	len = priv->ucode_code.len;
	IWL_DEBUG_INFO("Copying (but not loading) uCode instr len %Zd\n", len);
	memcpy(priv->ucode_code.v_addr, src, len);
	IWL_DEBUG_INFO("uCode instr buf vaddr = 0x%p, paddr = 0x%08x\n",
		priv->ucode_code.v_addr, (u32)priv->ucode_code.p_addr);

	/* Runtime data (2nd block)
	 * NOTE:  Copy into backup buffer will be done in iwl4965_up()  */
	src = &ucode->data[inst_size];
	len = priv->ucode_data.len;
	IWL_DEBUG_INFO("Copying (but not loading) uCode data len %Zd\n", len);
	memcpy(priv->ucode_data.v_addr, src, len);
	memcpy(priv->ucode_data_backup.v_addr, src, len);

	/* Initialization instructions (3rd block) */
	if (init_size) {
		src = &ucode->data[inst_size + data_size];
		len = priv->ucode_init.len;
		IWL_DEBUG_INFO("Copying (but not loading) init instr len %Zd\n",
				len);
		memcpy(priv->ucode_init.v_addr, src, len);
	}

	/* Initialization data (4th block) */
	if (init_data_size) {
		src = &ucode->data[inst_size + data_size + init_size];
		len = priv->ucode_init_data.len;
		IWL_DEBUG_INFO("Copying (but not loading) init data len %Zd\n",
			       len);
		memcpy(priv->ucode_init_data.v_addr, src, len);
	}

	/* Bootstrap instructions (5th block) */
	src = &ucode->data[inst_size + data_size + init_size + init_data_size];
	len = priv->ucode_boot.len;
	IWL_DEBUG_INFO("Copying (but not loading) boot instr len %Zd\n", len);
	memcpy(priv->ucode_boot.v_addr, src, len);

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);
	return 0;

 err_pci_alloc:
	IWL_ERROR("failed to allocate pci memory\n");
	ret = -ENOMEM;
	iwl4965_dealloc_ucode_pci(priv);

 err_release:
	release_firmware(ucode_raw);

 error:
	return ret;
}


/**
 * iwl4965_set_ucode_ptrs - Set uCode address location
 *
 * Tell initialization uCode where to find runtime uCode.
 *
 * BSM registers initially contain pointers to initialization uCode.
 * We need to replace them to load runtime uCode inst and data,
 * and to save runtime data when powering down.
 */
static int iwl4965_set_ucode_ptrs(struct iwl_priv *priv)
{
	dma_addr_t pinst;
	dma_addr_t pdata;
	int rc = 0;
	unsigned long flags;

	/* bits 35:4 for 4965 */
	pinst = priv->ucode_code.p_addr >> 4;
	pdata = priv->ucode_data_backup.p_addr >> 4;

	spin_lock_irqsave(&priv->lock, flags);
	rc = iwl_grab_nic_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}

	/* Tell bootstrap uCode where to find image to load */
	iwl_write_prph(priv, BSM_DRAM_INST_PTR_REG, pinst);
	iwl_write_prph(priv, BSM_DRAM_DATA_PTR_REG, pdata);
	iwl_write_prph(priv, BSM_DRAM_DATA_BYTECOUNT_REG,
				 priv->ucode_data.len);

	/* Inst bytecount must be last to set up, bit 31 signals uCode
	 *   that all new ptr/size info is in place */
	iwl_write_prph(priv, BSM_DRAM_INST_BYTECOUNT_REG,
				 priv->ucode_code.len | BSM_DRAM_INST_LOAD);

	iwl_release_nic_access(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_INFO("Runtime uCode pointers are set.\n");

	return rc;
}

/**
 * iwl4965_init_alive_start - Called after REPLY_ALIVE notification received
 *
 * Called after REPLY_ALIVE notification received from "initialize" uCode.
 *
 * The 4965 "initialize" ALIVE reply contains calibration data for:
 *   Voltage, temperature, and MIMO tx gain correction, now stored in priv
 *   (3945 does not contain this data).
 *
 * Tell "initialize" uCode to go ahead and load the runtime uCode.
*/
static void iwl4965_init_alive_start(struct iwl_priv *priv)
{
	/* Check alive response for "valid" sign from uCode */
	if (priv->card_alive_init.is_valid != UCODE_VALID_OK) {
		/* We had an error bringing up the hardware, so take it
		 * all the way back down so we can try again */
		IWL_DEBUG_INFO("Initialize Alive failed.\n");
		goto restart;
	}

	/* Bootstrap uCode has loaded initialize uCode ... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "initialize" alive if code weren't properly loaded.  */
	if (iwl4965_verify_ucode(priv)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO("Bad \"initialize\" uCode load.\n");
		goto restart;
	}

	/* Calculate temperature */
	priv->temperature = iwl4965_get_temperature(priv);

	/* Send pointers to protocol/runtime uCode image ... init code will
	 * load and launch runtime uCode, which will send us another "Alive"
	 * notification. */
	IWL_DEBUG_INFO("Initialization Alive received.\n");
	if (iwl4965_set_ucode_ptrs(priv)) {
		/* Runtime instruction load won't happen;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO("Couldn't set up uCode pointers.\n");
		goto restart;
	}
	return;

 restart:
	queue_work(priv->workqueue, &priv->restart);
}


/**
 * iwl4965_alive_start - called after REPLY_ALIVE notification received
 *                   from protocol/runtime uCode (initialization uCode's
 *                   Alive gets handled by iwl4965_init_alive_start()).
 */
static void iwl4965_alive_start(struct iwl_priv *priv)
{
	int ret = 0;

	IWL_DEBUG_INFO("Runtime Alive received.\n");

	if (priv->card_alive.is_valid != UCODE_VALID_OK) {
		/* We had an error bringing up the hardware, so take it
		 * all the way back down so we can try again */
		IWL_DEBUG_INFO("Alive failed.\n");
		goto restart;
	}

	/* Initialize uCode has loaded Runtime uCode ... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "runtime" alive if code weren't properly loaded.  */
	if (iwl4965_verify_ucode(priv)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO("Bad runtime uCode load.\n");
		goto restart;
	}

	iwlcore_clear_stations_table(priv);

	ret = priv->cfg->ops->lib->alive_notify(priv);
	if (ret) {
		IWL_WARNING("Could not complete ALIVE transition [ntf]: %d\n",
			    ret);
		goto restart;
	}

	/* After the ALIVE response, we can send host commands to 4965 uCode */
	set_bit(STATUS_ALIVE, &priv->status);

	/* Clear out the uCode error bit if it is set */
	clear_bit(STATUS_FW_ERROR, &priv->status);

	if (iwl_is_rfkill(priv))
		return;

	ieee80211_start_queues(priv->hw);

	priv->active_rate = priv->rates_mask;
	priv->active_rate_basic = priv->rates_mask & IWL_BASIC_RATES_MASK;

	iwl4965_send_power_mode(priv, IWL_POWER_LEVEL(priv->power_mode));

	if (iwl_is_associated(priv)) {
		struct iwl4965_rxon_cmd *active_rxon =
				(struct iwl4965_rxon_cmd *)(&priv->active_rxon);

		memcpy(&priv->staging_rxon, &priv->active_rxon,
		       sizeof(priv->staging_rxon));
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	} else {
		/* Initialize our rx_config data */
		iwl4965_connection_init_rx_config(priv);
		memcpy(priv->staging_rxon.node_addr, priv->mac_addr, ETH_ALEN);
	}

	/* Configure Bluetooth device coexistence support */
	iwl4965_send_bt_config(priv);

	/* Configure the adapter for unassociated operation */
	iwl4965_commit_rxon(priv);

	/* At this point, the NIC is initialized and operational */
	priv->notif_missed_beacons = 0;

	iwl4965_rf_kill_ct_config(priv);

	iwl_leds_register(priv);

	IWL_DEBUG_INFO("ALIVE processing complete.\n");
	set_bit(STATUS_READY, &priv->status);
	wake_up_interruptible(&priv->wait_command_queue);

	if (priv->error_recovering)
		iwl4965_error_recovery(priv);

	iwlcore_low_level_notify(priv, IWLCORE_START_EVT);
	ieee80211_notify_mac(priv->hw, IEEE80211_NOTIFY_RE_ASSOC);
	return;

 restart:
	queue_work(priv->workqueue, &priv->restart);
}

static void iwl4965_cancel_deferred_work(struct iwl_priv *priv);

static void __iwl4965_down(struct iwl_priv *priv)
{
	unsigned long flags;
	int exit_pending = test_bit(STATUS_EXIT_PENDING, &priv->status);
	struct ieee80211_conf *conf = NULL;

	IWL_DEBUG_INFO(DRV_NAME " is going down\n");

	conf = ieee80211_get_hw_conf(priv->hw);

	if (!exit_pending)
		set_bit(STATUS_EXIT_PENDING, &priv->status);

	iwl_leds_unregister(priv);

	iwlcore_low_level_notify(priv, IWLCORE_STOP_EVT);

	iwlcore_clear_stations_table(priv);

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
	iwl4965_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
	iwl_synchronize_irq(priv);

	if (priv->mac80211_registered)
		ieee80211_stop_queues(priv->hw);

	/* If we have not previously called iwl4965_init() then
	 * clear all bits but the RF Kill and SUSPEND bits and return */
	if (!iwl_is_init(priv)) {
		priv->status = test_bit(STATUS_RF_KILL_HW, &priv->status) <<
					STATUS_RF_KILL_HW |
			       test_bit(STATUS_RF_KILL_SW, &priv->status) <<
					STATUS_RF_KILL_SW |
			       test_bit(STATUS_GEO_CONFIGURED, &priv->status) <<
					STATUS_GEO_CONFIGURED |
			       test_bit(STATUS_IN_SUSPEND, &priv->status) <<
					STATUS_IN_SUSPEND;
		goto exit;
	}

	/* ...otherwise clear out all the status bits but the RF Kill and
	 * SUSPEND bits and continue taking the NIC down. */
	priv->status &= test_bit(STATUS_RF_KILL_HW, &priv->status) <<
				STATUS_RF_KILL_HW |
			test_bit(STATUS_RF_KILL_SW, &priv->status) <<
				STATUS_RF_KILL_SW |
			test_bit(STATUS_GEO_CONFIGURED, &priv->status) <<
				STATUS_GEO_CONFIGURED |
			test_bit(STATUS_IN_SUSPEND, &priv->status) <<
				STATUS_IN_SUSPEND |
			test_bit(STATUS_FW_ERROR, &priv->status) <<
				STATUS_FW_ERROR;

	spin_lock_irqsave(&priv->lock, flags);
	iwl_clear_bit(priv, CSR_GP_CNTRL,
			 CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl4965_hw_txq_ctx_stop(priv);
	iwl4965_hw_rxq_stop(priv);

	spin_lock_irqsave(&priv->lock, flags);
	if (!iwl_grab_nic_access(priv)) {
		iwl_write_prph(priv, APMG_CLK_DIS_REG,
					 APMG_CLK_VAL_DMA_CLK_RQT);
		iwl_release_nic_access(priv);
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	udelay(5);

	iwl4965_hw_nic_stop_master(priv);
	iwl_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);
	iwl4965_hw_nic_reset(priv);

 exit:
	memset(&priv->card_alive, 0, sizeof(struct iwl4965_alive_resp));

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);
	priv->ibss_beacon = NULL;

	/* clear out any free frames */
	iwl4965_clear_free_frames(priv);
}

static void iwl4965_down(struct iwl_priv *priv)
{
	mutex_lock(&priv->mutex);
	__iwl4965_down(priv);
	mutex_unlock(&priv->mutex);

	iwl4965_cancel_deferred_work(priv);
}

#define MAX_HW_RESTARTS 5

static int __iwl4965_up(struct iwl_priv *priv)
{
	int i;
	int ret;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_WARNING("Exit pending; will not bring the NIC up\n");
		return -EIO;
	}

	if (test_bit(STATUS_RF_KILL_SW, &priv->status)) {
		IWL_WARNING("Radio disabled by SW RF kill (module "
			    "parameter)\n");
		iwl_rfkill_set_hw_state(priv);
		return -ENODEV;
	}

	if (!priv->ucode_data_backup.v_addr || !priv->ucode_data.v_addr) {
		IWL_ERROR("ucode not available for device bringup\n");
		return -EIO;
	}

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (iwl_read32(priv, CSR_GP_CNTRL) &
				CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &priv->status);
	else {
		set_bit(STATUS_RF_KILL_HW, &priv->status);
		if (!test_bit(STATUS_IN_SUSPEND, &priv->status)) {
			iwl_rfkill_set_hw_state(priv);
			IWL_WARNING("Radio disabled by HW RF Kill switch\n");
			return -ENODEV;
		}
	}

	iwl_rfkill_set_hw_state(priv);
	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);

	ret = priv->cfg->ops->lib->hw_nic_init(priv);
	if (ret) {
		IWL_ERROR("Unable to init nic\n");
		return ret;
	}

	/* make sure rfkill handshake bits are cleared */
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable host interrupts */
	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);
	iwl4965_enable_interrupts(priv);

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

		iwlcore_clear_stations_table(priv);

		/* load bootstrap state machine,
		 * load bootstrap program into processor's memory,
		 * prepare to load the "initialize" uCode */
		ret = priv->cfg->ops->lib->load_ucode(priv);

		if (ret) {
			IWL_ERROR("Unable to set up bootstrap uCode: %d\n", ret);
			continue;
		}

		/* start card; "initialize" will load runtime ucode */
		iwl4965_nic_start(priv);

		IWL_DEBUG_INFO(DRV_NAME " is coming up\n");

		return 0;
	}

	set_bit(STATUS_EXIT_PENDING, &priv->status);
	__iwl4965_down(priv);

	/* tried to restart and config the device for as long as our
	 * patience could withstand */
	IWL_ERROR("Unable to initialize device after %d attempts.\n", i);
	return -EIO;
}


/*****************************************************************************
 *
 * Workqueue callbacks
 *
 *****************************************************************************/

static void iwl4965_bg_init_alive_start(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, init_alive_start.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwl4965_init_alive_start(priv);
	mutex_unlock(&priv->mutex);
}

static void iwl4965_bg_alive_start(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, alive_start.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwl4965_alive_start(priv);
	mutex_unlock(&priv->mutex);
}

static void iwl4965_bg_rf_kill(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv, rf_kill);

	wake_up_interruptible(&priv->wait_command_queue);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);

	if (!iwl_is_rfkill(priv)) {
		IWL_DEBUG(IWL_DL_INFO | IWL_DL_RF_KILL,
			  "HW and/or SW RF Kill no longer active, restarting "
			  "device\n");
		if (!test_bit(STATUS_EXIT_PENDING, &priv->status))
			queue_work(priv->workqueue, &priv->restart);
	} else {
		/* make sure mac80211 stop sending Tx frame */
		if (priv->mac80211_registered)
			ieee80211_stop_queues(priv->hw);

		if (!test_bit(STATUS_RF_KILL_HW, &priv->status))
			IWL_DEBUG_RF_KILL("Can not turn radio back on - "
					  "disabled by SW switch\n");
		else
			IWL_WARNING("Radio Frequency Kill Switch is On:\n"
				    "Kill switch must be turned off for "
				    "wireless networking to work.\n");
	}
	iwl_rfkill_set_hw_state(priv);

	mutex_unlock(&priv->mutex);
}

#define IWL_SCAN_CHECK_WATCHDOG (7 * HZ)

static void iwl4965_bg_scan_check(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, scan_check.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	if (test_bit(STATUS_SCANNING, &priv->status) ||
	    test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG(IWL_DL_INFO | IWL_DL_SCAN,
			  "Scan completion watchdog resetting adapter (%dms)\n",
			  jiffies_to_msecs(IWL_SCAN_CHECK_WATCHDOG));

		if (!test_bit(STATUS_EXIT_PENDING, &priv->status))
			iwl4965_send_scan_abort(priv);
	}
	mutex_unlock(&priv->mutex);
}

static void iwl4965_bg_request_scan(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, request_scan);
	struct iwl_host_cmd cmd = {
		.id = REPLY_SCAN_CMD,
		.len = sizeof(struct iwl4965_scan_cmd),
		.meta.flags = CMD_SIZE_HUGE,
	};
	struct iwl4965_scan_cmd *scan;
	struct ieee80211_conf *conf = NULL;
	u16 cmd_len;
	enum ieee80211_band band;
	u8 direct_mask;
	int ret = 0;

	conf = ieee80211_get_hw_conf(priv->hw);

	mutex_lock(&priv->mutex);

	if (!iwl_is_ready(priv)) {
		IWL_WARNING("request scan called when driver not ready.\n");
		goto done;
	}

	/* Make sure the scan wasn't cancelled before this queued work
	 * was given the chance to run... */
	if (!test_bit(STATUS_SCANNING, &priv->status))
		goto done;

	/* This should never be called or scheduled if there is currently
	 * a scan active in the hardware. */
	if (test_bit(STATUS_SCAN_HW, &priv->status)) {
		IWL_DEBUG_INFO("Multiple concurrent scan requests in parallel. "
			       "Ignoring second request.\n");
		ret = -EIO;
		goto done;
	}

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_DEBUG_SCAN("Aborting scan due to device shutdown\n");
		goto done;
	}

	if (test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_HC("Scan request while abort pending.  Queuing.\n");
		goto done;
	}

	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_HC("Aborting scan due to RF Kill activation\n");
		goto done;
	}

	if (!test_bit(STATUS_READY, &priv->status)) {
		IWL_DEBUG_HC("Scan request while uninitialized.  Queuing.\n");
		goto done;
	}

	if (!priv->scan_bands) {
		IWL_DEBUG_HC("Aborting scan due to no requested bands\n");
		goto done;
	}

	if (!priv->scan) {
		priv->scan = kmalloc(sizeof(struct iwl4965_scan_cmd) +
				     IWL_MAX_SCAN_SIZE, GFP_KERNEL);
		if (!priv->scan) {
			ret = -ENOMEM;
			goto done;
		}
	}
	scan = priv->scan;
	memset(scan, 0, sizeof(struct iwl4965_scan_cmd) + IWL_MAX_SCAN_SIZE);

	scan->quiet_plcp_th = IWL_PLCP_QUIET_THRESH;
	scan->quiet_time = IWL_ACTIVE_QUIET_TIME;

	if (iwl_is_associated(priv)) {
		u16 interval = 0;
		u32 extra;
		u32 suspend_time = 100;
		u32 scan_suspend_time = 100;
		unsigned long flags;

		IWL_DEBUG_INFO("Scanning while associated...\n");

		spin_lock_irqsave(&priv->lock, flags);
		interval = priv->beacon_int;
		spin_unlock_irqrestore(&priv->lock, flags);

		scan->suspend_time = 0;
		scan->max_out_time = cpu_to_le32(200 * 1024);
		if (!interval)
			interval = suspend_time;

		extra = (suspend_time / interval) << 22;
		scan_suspend_time = (extra |
		    ((suspend_time % interval) * 1024));
		scan->suspend_time = cpu_to_le32(scan_suspend_time);
		IWL_DEBUG_SCAN("suspend_time 0x%X beacon interval %d\n",
			       scan_suspend_time, interval);
	}

	/* We should add the ability for user to lock to PASSIVE ONLY */
	if (priv->one_direct_scan) {
		IWL_DEBUG_SCAN
		    ("Kicking off one direct scan for '%s'\n",
		     iwl4965_escape_essid(priv->direct_ssid,
				      priv->direct_ssid_len));
		scan->direct_scan[0].id = WLAN_EID_SSID;
		scan->direct_scan[0].len = priv->direct_ssid_len;
		memcpy(scan->direct_scan[0].ssid,
		       priv->direct_ssid, priv->direct_ssid_len);
		direct_mask = 1;
	} else if (!iwl_is_associated(priv) && priv->essid_len) {
		IWL_DEBUG_SCAN
		  ("Kicking off one direct scan for '%s' when not associated\n",
		   iwl4965_escape_essid(priv->essid, priv->essid_len));
		scan->direct_scan[0].id = WLAN_EID_SSID;
		scan->direct_scan[0].len = priv->essid_len;
		memcpy(scan->direct_scan[0].ssid, priv->essid, priv->essid_len);
		direct_mask = 1;
	} else {
		IWL_DEBUG_SCAN("Kicking off one indirect scan.\n");
		direct_mask = 0;
	}

	scan->tx_cmd.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK;
	scan->tx_cmd.sta_id = priv->hw_params.bcast_sta_id;
	scan->tx_cmd.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;


	switch (priv->scan_bands) {
	case 2:
		scan->flags = RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK;
		scan->tx_cmd.rate_n_flags =
				iwl4965_hw_set_rate_n_flags(IWL_RATE_1M_PLCP,
				RATE_MCS_ANT_B_MSK|RATE_MCS_CCK_MSK);

		scan->good_CRC_th = 0;
		band = IEEE80211_BAND_2GHZ;
		break;

	case 1:
		scan->tx_cmd.rate_n_flags =
				iwl4965_hw_set_rate_n_flags(IWL_RATE_6M_PLCP,
				RATE_MCS_ANT_B_MSK);
		scan->good_CRC_th = IWL_GOOD_CRC_TH;
		band = IEEE80211_BAND_5GHZ;
		break;

	default:
		IWL_WARNING("Invalid scan band count\n");
		goto done;
	}

	/* We don't build a direct scan probe request; the uCode will do
	 * that based on the direct_mask added to each channel entry */
	cmd_len = iwl4965_fill_probe_req(priv, band,
					(struct ieee80211_mgmt *)scan->data,
					IWL_MAX_SCAN_SIZE - sizeof(*scan), 0);

	scan->tx_cmd.len = cpu_to_le16(cmd_len);
	/* select Rx chains */

	/* Force use of chains B and C (0x6) for scan Rx.
	 * Avoid A (0x1) because of its off-channel reception on A-band.
	 * MIMO is not used here, but value is required to make uCode happy. */
	scan->rx_chain = RXON_RX_CHAIN_DRIVER_FORCE_MSK |
			cpu_to_le16((0x7 << RXON_RX_CHAIN_VALID_POS) |
			(0x6 << RXON_RX_CHAIN_FORCE_SEL_POS) |
			(0x7 << RXON_RX_CHAIN_FORCE_MIMO_SEL_POS));

	if (priv->iw_mode == IEEE80211_IF_TYPE_MNTR)
		scan->filter_flags = RXON_FILTER_PROMISC_MSK;

	if (direct_mask)
		scan->channel_count =
			iwl4965_get_channels_for_scan(
				priv, band, 1, /* active */
				direct_mask,
				(void *)&scan->data[le16_to_cpu(scan->tx_cmd.len)]);
	else
		scan->channel_count =
			iwl4965_get_channels_for_scan(
				priv, band, 0, /* passive */
				direct_mask,
				(void *)&scan->data[le16_to_cpu(scan->tx_cmd.len)]);

	cmd.len += le16_to_cpu(scan->tx_cmd.len) +
	    scan->channel_count * sizeof(struct iwl4965_scan_channel);
	cmd.data = scan;
	scan->len = cpu_to_le16(cmd.len);

	set_bit(STATUS_SCAN_HW, &priv->status);
	ret = iwl_send_cmd_sync(priv, &cmd);
	if (ret)
		goto done;

	queue_delayed_work(priv->workqueue, &priv->scan_check,
			   IWL_SCAN_CHECK_WATCHDOG);

	mutex_unlock(&priv->mutex);
	return;

 done:
	/* inform mac80211 scan aborted */
	queue_work(priv->workqueue, &priv->scan_completed);
	mutex_unlock(&priv->mutex);
}

static void iwl4965_bg_up(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, up);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	__iwl4965_up(priv);
	mutex_unlock(&priv->mutex);
}

static void iwl4965_bg_restart(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, restart);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	iwl4965_down(priv);
	queue_work(priv->workqueue, &priv->up);
}

static void iwl4965_bg_rx_replenish(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, rx_replenish);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwl4965_rx_replenish(priv);
	mutex_unlock(&priv->mutex);
}

#define IWL_DELAY_NEXT_SCAN (HZ*2)

static void iwl4965_post_associate(struct iwl_priv *priv)
{
	struct ieee80211_conf *conf = NULL;
	int ret = 0;
	DECLARE_MAC_BUF(mac);

	if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {
		IWL_ERROR("%s Should not be called in AP mode\n", __FUNCTION__);
		return;
	}

	IWL_DEBUG_ASSOC("Associated as %d to: %s\n",
			priv->assoc_id,
			print_mac(mac, priv->active_rxon.bssid_addr));


	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;


	if (!priv->vif || !priv->is_open)
		return;

	iwl4965_scan_cancel_timeout(priv, 200);

	conf = ieee80211_get_hw_conf(priv->hw);

	priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwl4965_commit_rxon(priv);

	memset(&priv->rxon_timing, 0, sizeof(struct iwl4965_rxon_time_cmd));
	iwl4965_setup_rxon_timing(priv);
	ret = iwl_send_cmd_pdu(priv, REPLY_RXON_TIMING,
			      sizeof(priv->rxon_timing), &priv->rxon_timing);
	if (ret)
		IWL_WARNING("REPLY_RXON_TIMING failed - "
			    "Attempting to continue.\n");

	priv->staging_rxon.filter_flags |= RXON_FILTER_ASSOC_MSK;

#ifdef CONFIG_IWL4965_HT
	if (priv->current_ht_config.is_ht)
		iwl4965_set_rxon_ht(priv, &priv->current_ht_config);
#endif /* CONFIG_IWL4965_HT*/
	iwl4965_set_rxon_chain(priv);
	priv->staging_rxon.assoc_id = cpu_to_le16(priv->assoc_id);

	IWL_DEBUG_ASSOC("assoc id %d beacon interval %d\n",
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

		if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS)
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

	}

	iwl4965_commit_rxon(priv);

	switch (priv->iw_mode) {
	case IEEE80211_IF_TYPE_STA:
		iwl4965_rate_scale_init(priv->hw, IWL_AP_ID);
		break;

	case IEEE80211_IF_TYPE_IBSS:

		/* clear out the station table */
		iwlcore_clear_stations_table(priv);

		iwl4965_rxon_add_station(priv, iwl4965_broadcast_addr, 0);
		iwl4965_rxon_add_station(priv, priv->bssid, 0);
		iwl4965_rate_scale_init(priv->hw, IWL_STA_ID);
		iwl4965_send_beacon_cmd(priv);

		break;

	default:
		IWL_ERROR("%s Should not be called in %d mode\n",
				__FUNCTION__, priv->iw_mode);
		break;
	}

	iwl4965_sequence_reset(priv);

#ifdef CONFIG_IWL4965_SENSITIVITY
	/* Enable Rx differential gain and sensitivity calibrations */
	iwl4965_chain_noise_reset(priv);
	priv->start_calib = 1;
#endif /* CONFIG_IWL4965_SENSITIVITY */

	if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS)
		priv->assoc_station_added = 1;

	iwl4965_activate_qos(priv, 0);

	/* we have just associated, don't start scan too early */
	priv->next_scan_jiffies = jiffies + IWL_DELAY_NEXT_SCAN;
}


static void iwl4965_bg_post_associate(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv,
					     post_associate.work);

	mutex_lock(&priv->mutex);
	iwl4965_post_associate(priv);
	mutex_unlock(&priv->mutex);

}

static void iwl4965_bg_abort_scan(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv, abort_scan);

	if (!iwl_is_ready(priv))
		return;

	mutex_lock(&priv->mutex);

	set_bit(STATUS_SCAN_ABORTING, &priv->status);
	iwl4965_send_scan_abort(priv);

	mutex_unlock(&priv->mutex);
}

static int iwl4965_mac_config(struct ieee80211_hw *hw, struct ieee80211_conf *conf);

static void iwl4965_bg_scan_completed(struct work_struct *work)
{
	struct iwl_priv *priv =
	    container_of(work, struct iwl_priv, scan_completed);

	IWL_DEBUG(IWL_DL_INFO | IWL_DL_SCAN, "SCAN complete scan\n");

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (test_bit(STATUS_CONF_PENDING, &priv->status))
		iwl4965_mac_config(priv->hw, ieee80211_get_hw_conf(priv->hw));

	ieee80211_scan_completed(priv->hw);

	/* Since setting the TXPOWER may have been deferred while
	 * performing the scan, fire one off */
	mutex_lock(&priv->mutex);
	iwl4965_hw_reg_send_txpower(priv);
	mutex_unlock(&priv->mutex);
}

/*****************************************************************************
 *
 * mac80211 entry point functions
 *
 *****************************************************************************/

#define UCODE_READY_TIMEOUT	(2 * HZ)

static int iwl4965_mac_start(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	int ret;

	IWL_DEBUG_MAC80211("enter\n");

	if (pci_enable_device(priv->pci_dev)) {
		IWL_ERROR("Fail to pci_enable_device\n");
		return -ENODEV;
	}
	pci_restore_state(priv->pci_dev);
	pci_enable_msi(priv->pci_dev);

	ret = request_irq(priv->pci_dev->irq, iwl4965_isr, IRQF_SHARED,
			  DRV_NAME, priv);
	if (ret) {
		IWL_ERROR("Error allocating IRQ %d\n", priv->pci_dev->irq);
		goto out_disable_msi;
	}

	/* we should be verifying the device is ready to be opened */
	mutex_lock(&priv->mutex);

	memset(&priv->staging_rxon, 0, sizeof(struct iwl4965_rxon_cmd));
	/* fetch ucode file from disk, alloc and copy to bus-master buffers ...
	 * ucode filename and max sizes are card-specific. */

	if (!priv->ucode_code.len) {
		ret = iwl4965_read_ucode(priv);
		if (ret) {
			IWL_ERROR("Could not read microcode: %d\n", ret);
			mutex_unlock(&priv->mutex);
			goto out_release_irq;
		}
	}

	ret = __iwl4965_up(priv);

	mutex_unlock(&priv->mutex);

	if (ret)
		goto out_release_irq;

	IWL_DEBUG_INFO("Start UP work done.\n");

	if (test_bit(STATUS_IN_SUSPEND, &priv->status))
		return 0;

	/* Wait for START_ALIVE from ucode. Otherwise callbacks from
	 * mac80211 will not be run successfully. */
	ret = wait_event_interruptible_timeout(priv->wait_command_queue,
			test_bit(STATUS_READY, &priv->status),
			UCODE_READY_TIMEOUT);
	if (!ret) {
		if (!test_bit(STATUS_READY, &priv->status)) {
			IWL_ERROR("Wait for START_ALIVE timeout after %dms.\n",
				  jiffies_to_msecs(UCODE_READY_TIMEOUT));
			ret = -ETIMEDOUT;
			goto out_release_irq;
		}
	}

	priv->is_open = 1;
	IWL_DEBUG_MAC80211("leave\n");
	return 0;

out_release_irq:
	free_irq(priv->pci_dev->irq, priv);
out_disable_msi:
	pci_disable_msi(priv->pci_dev);
	pci_disable_device(priv->pci_dev);
	priv->is_open = 0;
	IWL_DEBUG_MAC80211("leave - failed\n");
	return ret;
}

static void iwl4965_mac_stop(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211("enter\n");

	if (!priv->is_open) {
		IWL_DEBUG_MAC80211("leave - skip\n");
		return;
	}

	priv->is_open = 0;

	if (iwl_is_ready_rf(priv)) {
		/* stop mac, cancel any scan request and clear
		 * RXON_FILTER_ASSOC_MSK BIT
		 */
		mutex_lock(&priv->mutex);
		iwl4965_scan_cancel_timeout(priv, 100);
		cancel_delayed_work(&priv->post_associate);
		mutex_unlock(&priv->mutex);
	}

	iwl4965_down(priv);

	flush_workqueue(priv->workqueue);
	free_irq(priv->pci_dev->irq, priv);
	pci_disable_msi(priv->pci_dev);
	pci_save_state(priv->pci_dev);
	pci_disable_device(priv->pci_dev);

	IWL_DEBUG_MAC80211("leave\n");
}

static int iwl4965_mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb,
		      struct ieee80211_tx_control *ctl)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211("enter\n");

	if (priv->iw_mode == IEEE80211_IF_TYPE_MNTR) {
		IWL_DEBUG_MAC80211("leave - monitor\n");
		return -1;
	}

	IWL_DEBUG_TX("dev->xmit(%d bytes) at rate 0x%02x\n", skb->len,
		     ctl->tx_rate->bitrate);

	if (iwl4965_tx_skb(priv, skb, ctl))
		dev_kfree_skb_any(skb);

	IWL_DEBUG_MAC80211("leave\n");
	return 0;
}

static int iwl4965_mac_add_interface(struct ieee80211_hw *hw,
				 struct ieee80211_if_init_conf *conf)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	DECLARE_MAC_BUF(mac);

	IWL_DEBUG_MAC80211("enter: type %d\n", conf->type);

	if (priv->vif) {
		IWL_DEBUG_MAC80211("leave - vif != NULL\n");
		return -EOPNOTSUPP;
	}

	spin_lock_irqsave(&priv->lock, flags);
	priv->vif = conf->vif;

	spin_unlock_irqrestore(&priv->lock, flags);

	mutex_lock(&priv->mutex);

	if (conf->mac_addr) {
		IWL_DEBUG_MAC80211("Set %s\n", print_mac(mac, conf->mac_addr));
		memcpy(priv->mac_addr, conf->mac_addr, ETH_ALEN);
	}

	if (iwl_is_ready(priv))
		iwl4965_set_mode(priv, conf->type);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211("leave\n");
	return 0;
}

/**
 * iwl4965_mac_config - mac80211 config callback
 *
 * We ignore conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME since it seems to
 * be set inappropriately and the driver currently sets the hardware up to
 * use it whenever needed.
 */
static int iwl4965_mac_config(struct ieee80211_hw *hw, struct ieee80211_conf *conf)
{
	struct iwl_priv *priv = hw->priv;
	const struct iwl_channel_info *ch_info;
	unsigned long flags;
	int ret = 0;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211("enter to channel %d\n", conf->channel->hw_value);

	priv->add_radiotap = !!(conf->flags & IEEE80211_CONF_RADIOTAP);

	if (!iwl_is_ready(priv)) {
		IWL_DEBUG_MAC80211("leave - not ready\n");
		ret = -EIO;
		goto out;
	}

	if (unlikely(!priv->cfg->mod_params->disable_hw_scan &&
		     test_bit(STATUS_SCANNING, &priv->status))) {
		IWL_DEBUG_MAC80211("leave - scanning\n");
		set_bit(STATUS_CONF_PENDING, &priv->status);
		mutex_unlock(&priv->mutex);
		return 0;
	}

	spin_lock_irqsave(&priv->lock, flags);

	ch_info = iwl_get_channel_info(priv, conf->channel->band,
			ieee80211_frequency_to_channel(conf->channel->center_freq));
	if (!is_channel_valid(ch_info)) {
		IWL_DEBUG_MAC80211("leave - invalid channel\n");
		spin_unlock_irqrestore(&priv->lock, flags);
		ret = -EINVAL;
		goto out;
	}

#ifdef CONFIG_IWL4965_HT
	/* if we are switching from ht to 2.4 clear flags
	 * from any ht related info since 2.4 does not
	 * support ht */
	if ((le16_to_cpu(priv->staging_rxon.channel) != conf->channel->hw_value)
#ifdef IEEE80211_CONF_CHANNEL_SWITCH
	    && !(conf->flags & IEEE80211_CONF_CHANNEL_SWITCH)
#endif
	)
		priv->staging_rxon.flags = 0;
#endif /* CONFIG_IWL4965_HT */

	iwlcore_set_rxon_channel(priv, conf->channel->band,
		ieee80211_frequency_to_channel(conf->channel->center_freq));

	iwl4965_set_flags_for_phymode(priv, conf->channel->band);

	/* The list of supported rates and rate mask can be different
	 * for each band; since the band may have changed, reset
	 * the rate mask to what mac80211 lists */
	iwl4965_set_rate(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

#ifdef IEEE80211_CONF_CHANNEL_SWITCH
	if (conf->flags & IEEE80211_CONF_CHANNEL_SWITCH) {
		iwl4965_hw_channel_switch(priv, conf->channel);
		goto out;
	}
#endif

	if (priv->cfg->ops->lib->radio_kill_sw)
		priv->cfg->ops->lib->radio_kill_sw(priv, !conf->radio_enabled);

	if (!conf->radio_enabled) {
		IWL_DEBUG_MAC80211("leave - radio disabled\n");
		goto out;
	}

	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_MAC80211("leave - RF kill\n");
		ret = -EIO;
		goto out;
	}

	iwl4965_set_rate(priv);

	if (memcmp(&priv->active_rxon,
		   &priv->staging_rxon, sizeof(priv->staging_rxon)))
		iwl4965_commit_rxon(priv);
	else
		IWL_DEBUG_INFO("No re-sending same RXON configuration.\n");

	IWL_DEBUG_MAC80211("leave\n");

out:
	clear_bit(STATUS_CONF_PENDING, &priv->status);
	mutex_unlock(&priv->mutex);
	return ret;
}

static void iwl4965_config_ap(struct iwl_priv *priv)
{
	int ret = 0;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* The following should be done only at AP bring up */
	if ((priv->active_rxon.filter_flags & RXON_FILTER_ASSOC_MSK) == 0) {

		/* RXON - unassoc (to set timing command) */
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl4965_commit_rxon(priv);

		/* RXON Timing */
		memset(&priv->rxon_timing, 0, sizeof(struct iwl4965_rxon_time_cmd));
		iwl4965_setup_rxon_timing(priv);
		ret = iwl_send_cmd_pdu(priv, REPLY_RXON_TIMING,
				sizeof(priv->rxon_timing), &priv->rxon_timing);
		if (ret)
			IWL_WARNING("REPLY_RXON_TIMING failed - "
					"Attempting to continue.\n");

		iwl4965_set_rxon_chain(priv);

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

			if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS)
				priv->staging_rxon.flags &=
					~RXON_FLG_SHORT_SLOT_MSK;
		}
		/* restore RXON assoc */
		priv->staging_rxon.filter_flags |= RXON_FILTER_ASSOC_MSK;
		iwl4965_commit_rxon(priv);
		iwl4965_activate_qos(priv, 1);
		iwl4965_rxon_add_station(priv, iwl4965_broadcast_addr, 0);
	}
	iwl4965_send_beacon_cmd(priv);

	/* FIXME - we need to add code here to detect a totally new
	 * configuration, reset the AP, unassoc, rxon timing, assoc,
	 * clear sta table, add BCAST sta... */
}

static int iwl4965_mac_config_interface(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
				    struct ieee80211_if_conf *conf)
{
	struct iwl_priv *priv = hw->priv;
	DECLARE_MAC_BUF(mac);
	unsigned long flags;
	int rc;

	if (conf == NULL)
		return -EIO;

	if (priv->vif != vif) {
		IWL_DEBUG_MAC80211("leave - priv->vif != vif\n");
		return 0;
	}

	if ((priv->iw_mode == IEEE80211_IF_TYPE_AP) &&
	    (!conf->beacon || !conf->ssid_len)) {
		IWL_DEBUG_MAC80211
		    ("Leaving in AP mode because HostAPD is not ready.\n");
		return 0;
	}

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	mutex_lock(&priv->mutex);

	if (conf->bssid)
		IWL_DEBUG_MAC80211("bssid: %s\n",
				   print_mac(mac, conf->bssid));

/*
 * very dubious code was here; the probe filtering flag is never set:
 *
	if (unlikely(test_bit(STATUS_SCANNING, &priv->status)) &&
	    !(priv->hw->flags & IEEE80211_HW_NO_PROBE_FILTERING)) {
 */

	if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {
		if (!conf->bssid) {
			conf->bssid = priv->mac_addr;
			memcpy(priv->bssid, priv->mac_addr, ETH_ALEN);
			IWL_DEBUG_MAC80211("bssid was set to: %s\n",
					   print_mac(mac, conf->bssid));
		}
		if (priv->ibss_beacon)
			dev_kfree_skb(priv->ibss_beacon);

		priv->ibss_beacon = conf->beacon;
	}

	if (iwl_is_rfkill(priv))
		goto done;

	if (conf->bssid && !is_zero_ether_addr(conf->bssid) &&
	    !is_multicast_ether_addr(conf->bssid)) {
		/* If there is currently a HW scan going on in the background
		 * then we need to cancel it else the RXON below will fail. */
		if (iwl4965_scan_cancel_timeout(priv, 100)) {
			IWL_WARNING("Aborted scan still in progress "
				    "after 100ms\n");
			IWL_DEBUG_MAC80211("leaving - scan abort failed.\n");
			mutex_unlock(&priv->mutex);
			return -EAGAIN;
		}
		memcpy(priv->staging_rxon.bssid_addr, conf->bssid, ETH_ALEN);

		/* TODO: Audit driver for usage of these members and see
		 * if mac80211 deprecates them (priv->bssid looks like it
		 * shouldn't be there, but I haven't scanned the IBSS code
		 * to verify) - jpk */
		memcpy(priv->bssid, conf->bssid, ETH_ALEN);

		if (priv->iw_mode == IEEE80211_IF_TYPE_AP)
			iwl4965_config_ap(priv);
		else {
			rc = iwl4965_commit_rxon(priv);
			if ((priv->iw_mode == IEEE80211_IF_TYPE_STA) && rc)
				iwl4965_rxon_add_station(
					priv, priv->active_rxon.bssid_addr, 1);
		}

	} else {
		iwl4965_scan_cancel_timeout(priv, 100);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl4965_commit_rxon(priv);
	}

 done:
	spin_lock_irqsave(&priv->lock, flags);
	if (!conf->ssid_len)
		memset(priv->essid, 0, IW_ESSID_MAX_SIZE);
	else
		memcpy(priv->essid, conf->ssid, conf->ssid_len);

	priv->essid_len = conf->ssid_len;
	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_MAC80211("leave\n");
	mutex_unlock(&priv->mutex);

	return 0;
}

static void iwl4965_configure_filter(struct ieee80211_hw *hw,
				 unsigned int changed_flags,
				 unsigned int *total_flags,
				 int mc_count, struct dev_addr_list *mc_list)
{
	/*
	 * XXX: dummy
	 * see also iwl4965_connection_init_rx_config
	 */
	*total_flags = 0;
}

static void iwl4965_mac_remove_interface(struct ieee80211_hw *hw,
				     struct ieee80211_if_init_conf *conf)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211("enter\n");

	mutex_lock(&priv->mutex);

	if (iwl_is_ready_rf(priv)) {
		iwl4965_scan_cancel_timeout(priv, 100);
		cancel_delayed_work(&priv->post_associate);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl4965_commit_rxon(priv);
	}
	if (priv->vif == conf->vif) {
		priv->vif = NULL;
		memset(priv->bssid, 0, ETH_ALEN);
		memset(priv->essid, 0, IW_ESSID_MAX_SIZE);
		priv->essid_len = 0;
	}
	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211("leave\n");

}


#ifdef CONFIG_IWL4965_HT
static void iwl4965_ht_conf(struct iwl_priv *priv,
			    struct ieee80211_bss_conf *bss_conf)
{
	struct ieee80211_ht_info *ht_conf = bss_conf->ht_conf;
	struct ieee80211_ht_bss_info *ht_bss_conf = bss_conf->ht_bss_conf;
	struct iwl_ht_info *iwl_conf = &priv->current_ht_config;

	IWL_DEBUG_MAC80211("enter: \n");

	iwl_conf->is_ht = bss_conf->assoc_ht;

	if (!iwl_conf->is_ht)
		return;

	priv->ps_mode = (u8)((ht_conf->cap & IEEE80211_HT_CAP_MIMO_PS) >> 2);

	if (ht_conf->cap & IEEE80211_HT_CAP_SGI_20)
		iwl_conf->sgf |= 0x1;
	if (ht_conf->cap & IEEE80211_HT_CAP_SGI_40)
		iwl_conf->sgf |= 0x2;

	iwl_conf->is_green_field = !!(ht_conf->cap & IEEE80211_HT_CAP_GRN_FLD);
	iwl_conf->max_amsdu_size =
		!!(ht_conf->cap & IEEE80211_HT_CAP_MAX_AMSDU);

	iwl_conf->supported_chan_width =
		!!(ht_conf->cap & IEEE80211_HT_CAP_SUP_WIDTH);
	iwl_conf->extension_chan_offset =
		ht_bss_conf->bss_cap & IEEE80211_HT_IE_CHA_SEC_OFFSET;
	/* If no above or below channel supplied disable FAT channel */
	if (iwl_conf->extension_chan_offset != IWL_EXT_CHANNEL_OFFSET_ABOVE &&
	    iwl_conf->extension_chan_offset != IWL_EXT_CHANNEL_OFFSET_BELOW)
		iwl_conf->supported_chan_width = 0;

	iwl_conf->tx_mimo_ps_mode =
		(u8)((ht_conf->cap & IEEE80211_HT_CAP_MIMO_PS) >> 2);
	memcpy(iwl_conf->supp_mcs_set, ht_conf->supp_mcs_set, 16);

	iwl_conf->control_channel = ht_bss_conf->primary_channel;
	iwl_conf->tx_chan_width =
		!!(ht_bss_conf->bss_cap & IEEE80211_HT_IE_CHA_WIDTH);
	iwl_conf->ht_protection =
		ht_bss_conf->bss_op_mode & IEEE80211_HT_IE_HT_PROTECTION;
	iwl_conf->non_GF_STA_present =
		!!(ht_bss_conf->bss_op_mode & IEEE80211_HT_IE_NON_GF_STA_PRSNT);

	IWL_DEBUG_MAC80211("control channel %d\n", iwl_conf->control_channel);
	IWL_DEBUG_MAC80211("leave\n");
}
#else
static inline void iwl4965_ht_conf(struct iwl_priv *priv,
				   struct ieee80211_bss_conf *bss_conf)
{
}
#endif

#define IWL_DELAY_NEXT_SCAN_AFTER_ASSOC (HZ*6)
static void iwl4965_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u32 changes)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211("changes = 0x%X\n", changes);

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		IWL_DEBUG_MAC80211("ERP_PREAMBLE %d\n",
				   bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			priv->staging_rxon.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	}

	if (changes & BSS_CHANGED_ERP_CTS_PROT) {
		IWL_DEBUG_MAC80211("ERP_CTS %d\n", bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot && (priv->band != IEEE80211_BAND_5GHZ))
			priv->staging_rxon.flags |= RXON_FLG_TGG_PROTECT_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_TGG_PROTECT_MSK;
	}

	if (changes & BSS_CHANGED_HT) {
		IWL_DEBUG_MAC80211("HT %d\n", bss_conf->assoc_ht);
		iwl4965_ht_conf(priv, bss_conf);
		iwl4965_set_rxon_chain(priv);
	}

	if (changes & BSS_CHANGED_ASSOC) {
		IWL_DEBUG_MAC80211("ASSOC %d\n", bss_conf->assoc);
		/* This should never happen as this function should
		 * never be called from interrupt context. */
		if (WARN_ON_ONCE(in_interrupt()))
			return;
		if (bss_conf->assoc) {
			priv->assoc_id = bss_conf->aid;
			priv->beacon_int = bss_conf->beacon_int;
			priv->timestamp = bss_conf->timestamp;
			priv->assoc_capability = bss_conf->assoc_capability;
			priv->next_scan_jiffies = jiffies +
					IWL_DELAY_NEXT_SCAN_AFTER_ASSOC;
			mutex_lock(&priv->mutex);
			iwl4965_post_associate(priv);
			mutex_unlock(&priv->mutex);
		} else {
			priv->assoc_id = 0;
			IWL_DEBUG_MAC80211("DISASSOC %d\n", bss_conf->assoc);
		}
	} else if (changes && iwl_is_associated(priv) && priv->assoc_id) {
			IWL_DEBUG_MAC80211("Associated Changes %d\n", changes);
			iwl_send_rxon_assoc(priv);
	}

}

static int iwl4965_mac_hw_scan(struct ieee80211_hw *hw, u8 *ssid, size_t len)
{
	int rc = 0;
	unsigned long flags;
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211("enter\n");

	mutex_lock(&priv->mutex);
	spin_lock_irqsave(&priv->lock, flags);

	if (!iwl_is_ready_rf(priv)) {
		rc = -EIO;
		IWL_DEBUG_MAC80211("leave - not ready or exit pending\n");
		goto out_unlock;
	}

	if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {	/* APs don't scan */
		rc = -EIO;
		IWL_ERROR("ERROR: APs don't scan\n");
		goto out_unlock;
	}

	/* we don't schedule scan within next_scan_jiffies period */
	if (priv->next_scan_jiffies &&
			time_after(priv->next_scan_jiffies, jiffies)) {
		rc = -EAGAIN;
		goto out_unlock;
	}
	/* if we just finished scan ask for delay */
	if (priv->last_scan_jiffies && time_after(priv->last_scan_jiffies +
				IWL_DELAY_NEXT_SCAN, jiffies)) {
		rc = -EAGAIN;
		goto out_unlock;
	}
	if (len) {
		IWL_DEBUG_SCAN("direct scan for %s [%d]\n ",
			       iwl4965_escape_essid(ssid, len), (int)len);

		priv->one_direct_scan = 1;
		priv->direct_ssid_len = (u8)
		    min((u8) len, (u8) IW_ESSID_MAX_SIZE);
		memcpy(priv->direct_ssid, ssid, priv->direct_ssid_len);
	} else
		priv->one_direct_scan = 0;

	rc = iwl4965_scan_initiate(priv);

	IWL_DEBUG_MAC80211("leave\n");

out_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
	mutex_unlock(&priv->mutex);

	return rc;
}

static void iwl4965_mac_update_tkip_key(struct ieee80211_hw *hw,
			struct ieee80211_key_conf *keyconf, const u8 *addr,
			u32 iv32, u16 *phase1key)
{
	struct iwl_priv *priv = hw->priv;
	u8 sta_id = IWL_INVALID_STATION;
	unsigned long flags;
	__le16 key_flags = 0;
	int i;
	DECLARE_MAC_BUF(mac);

	IWL_DEBUG_MAC80211("enter\n");

	sta_id = iwl4965_hw_find_station(priv, addr);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_DEBUG_MAC80211("leave - %s not in station map.\n",
				   print_mac(mac, addr));
		return;
	}

	iwl4965_scan_cancel_timeout(priv, 100);

	key_flags |= (STA_KEY_FLG_TKIP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == priv->hw_params.bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].sta.key.key_flags = key_flags;
	priv->stations[sta_id].sta.key.tkip_rx_tsc_byte2 = (u8) iv32;

	for (i = 0; i < 5; i++)
		priv->stations[sta_id].sta.key.tkip_rx_ttak[i] =
			cpu_to_le16(phase1key[i]);

	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	iwl4965_send_add_station(priv, &priv->stations[sta_id].sta, CMD_ASYNC);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	IWL_DEBUG_MAC80211("leave\n");
}

static int iwl4965_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			   const u8 *local_addr, const u8 *addr,
			   struct ieee80211_key_conf *key)
{
	struct iwl_priv *priv = hw->priv;
	DECLARE_MAC_BUF(mac);
	int ret = 0;
	u8 sta_id = IWL_INVALID_STATION;
	u8 is_default_wep_key = 0;

	IWL_DEBUG_MAC80211("enter\n");

	if (priv->cfg->mod_params->sw_crypto) {
		IWL_DEBUG_MAC80211("leave - hwcrypto disabled\n");
		return -EOPNOTSUPP;
	}

	if (is_zero_ether_addr(addr))
		/* only support pairwise keys */
		return -EOPNOTSUPP;

	sta_id = iwl4965_hw_find_station(priv, addr);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_DEBUG_MAC80211("leave - %s not in station map.\n",
				   print_mac(mac, addr));
		return -EINVAL;

	}

	mutex_lock(&priv->mutex);
	iwl4965_scan_cancel_timeout(priv, 100);
	mutex_unlock(&priv->mutex);

	/* If we are getting WEP group key and we didn't receive any key mapping
	 * so far, we are in legacy wep mode (group key only), otherwise we are
	 * in 1X mode.
	 * In legacy wep mode, we use another host command to the uCode */
	if (key->alg == ALG_WEP && sta_id == priv->hw_params.bcast_sta_id &&
		priv->iw_mode != IEEE80211_IF_TYPE_AP) {
		if (cmd == SET_KEY)
			is_default_wep_key = !priv->key_mapping_key;
		else
			is_default_wep_key = priv->default_wep_key;
	}

	switch (cmd) {
	case SET_KEY:
		if (is_default_wep_key)
			ret = iwl_set_default_wep_key(priv, key);
		else
			ret = iwl_set_dynamic_key(priv, key, sta_id);

		IWL_DEBUG_MAC80211("enable hwcrypto key\n");
		break;
	case DISABLE_KEY:
		if (is_default_wep_key)
			ret = iwl_remove_default_wep_key(priv, key);
		else
			ret = iwl_remove_dynamic_key(priv, sta_id);

		IWL_DEBUG_MAC80211("disable hwcrypto key\n");
		break;
	default:
		ret = -EINVAL;
	}

	IWL_DEBUG_MAC80211("leave\n");

	return ret;
}

static int iwl4965_mac_conf_tx(struct ieee80211_hw *hw, int queue,
			   const struct ieee80211_tx_queue_params *params)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	int q;

	IWL_DEBUG_MAC80211("enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211("leave - RF not ready\n");
		return -EIO;
	}

	if (queue >= AC_NUM) {
		IWL_DEBUG_MAC80211("leave - queue >= AC_NUM %d\n", queue);
		return 0;
	}

	if (!priv->qos_data.qos_enable) {
		priv->qos_data.qos_active = 0;
		IWL_DEBUG_MAC80211("leave - qos not enabled\n");
		return 0;
	}
	q = AC_NUM - 1 - queue;

	spin_lock_irqsave(&priv->lock, flags);

	priv->qos_data.def_qos_parm.ac[q].cw_min = cpu_to_le16(params->cw_min);
	priv->qos_data.def_qos_parm.ac[q].cw_max = cpu_to_le16(params->cw_max);
	priv->qos_data.def_qos_parm.ac[q].aifsn = params->aifs;
	priv->qos_data.def_qos_parm.ac[q].edca_txop =
			cpu_to_le16((params->txop * 32));

	priv->qos_data.def_qos_parm.ac[q].reserved1 = 0;
	priv->qos_data.qos_active = 1;

	spin_unlock_irqrestore(&priv->lock, flags);

	mutex_lock(&priv->mutex);
	if (priv->iw_mode == IEEE80211_IF_TYPE_AP)
		iwl4965_activate_qos(priv, 1);
	else if (priv->assoc_id && iwl_is_associated(priv))
		iwl4965_activate_qos(priv, 0);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211("leave\n");
	return 0;
}

static int iwl4965_mac_get_tx_stats(struct ieee80211_hw *hw,
				struct ieee80211_tx_queue_stats *stats)
{
	struct iwl_priv *priv = hw->priv;
	int i, avail;
	struct iwl4965_tx_queue *txq;
	struct iwl4965_queue *q;
	unsigned long flags;

	IWL_DEBUG_MAC80211("enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211("leave - RF not ready\n");
		return -EIO;
	}

	spin_lock_irqsave(&priv->lock, flags);

	for (i = 0; i < AC_NUM; i++) {
		txq = &priv->txq[i];
		q = &txq->q;
		avail = iwl4965_queue_space(q);

		stats->data[i].len = q->n_window - avail;
		stats->data[i].limit = q->n_window - q->high_mark;
		stats->data[i].count = q->n_window;

	}
	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_MAC80211("leave\n");

	return 0;
}

static int iwl4965_mac_get_stats(struct ieee80211_hw *hw,
			     struct ieee80211_low_level_stats *stats)
{
	IWL_DEBUG_MAC80211("enter\n");
	IWL_DEBUG_MAC80211("leave\n");

	return 0;
}

static u64 iwl4965_mac_get_tsf(struct ieee80211_hw *hw)
{
	IWL_DEBUG_MAC80211("enter\n");
	IWL_DEBUG_MAC80211("leave\n");

	return 0;
}

static void iwl4965_mac_reset_tsf(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211("enter\n");

	priv->lq_mngr.lq_ready = 0;
#ifdef CONFIG_IWL4965_HT
	spin_lock_irqsave(&priv->lock, flags);
	memset(&priv->current_ht_config, 0, sizeof(struct iwl_ht_info));
	spin_unlock_irqrestore(&priv->lock, flags);
#endif /* CONFIG_IWL4965_HT */

	iwlcore_reset_qos(priv);

	cancel_delayed_work(&priv->post_associate);

	spin_lock_irqsave(&priv->lock, flags);
	priv->assoc_id = 0;
	priv->assoc_capability = 0;
	priv->assoc_station_added = 0;

	/* new association get rid of ibss beacon skb */
	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = NULL;

	priv->beacon_int = priv->hw->conf.beacon_int;
	priv->timestamp = 0;
	if ((priv->iw_mode == IEEE80211_IF_TYPE_STA))
		priv->beacon_int = 0;

	spin_unlock_irqrestore(&priv->lock, flags);

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211("leave - not ready\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	/* we are restarting association process
	 * clear RXON_FILTER_ASSOC_MSK bit
	 */
	if (priv->iw_mode != IEEE80211_IF_TYPE_AP) {
		iwl4965_scan_cancel_timeout(priv, 100);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl4965_commit_rxon(priv);
	}

	/* Per mac80211.h: This is only used in IBSS mode... */
	if (priv->iw_mode != IEEE80211_IF_TYPE_IBSS) {

		IWL_DEBUG_MAC80211("leave - not in IBSS\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	iwl4965_set_rate(priv);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211("leave\n");
}

static int iwl4965_mac_beacon_update(struct ieee80211_hw *hw, struct sk_buff *skb,
				 struct ieee80211_tx_control *control)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211("enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211("leave - RF not ready\n");
		mutex_unlock(&priv->mutex);
		return -EIO;
	}

	if (priv->iw_mode != IEEE80211_IF_TYPE_IBSS) {
		IWL_DEBUG_MAC80211("leave - not IBSS\n");
		mutex_unlock(&priv->mutex);
		return -EIO;
	}

	spin_lock_irqsave(&priv->lock, flags);

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = skb;

	priv->assoc_id = 0;

	IWL_DEBUG_MAC80211("leave\n");
	spin_unlock_irqrestore(&priv->lock, flags);

	iwlcore_reset_qos(priv);

	queue_work(priv->workqueue, &priv->post_associate.work);

	mutex_unlock(&priv->mutex);

	return 0;
}

/*****************************************************************************
 *
 * sysfs attributes
 *
 *****************************************************************************/

#ifdef CONFIG_IWLWIFI_DEBUG

/*
 * The following adds a new attribute to the sysfs representation
 * of this device driver (i.e. a new file in /sys/bus/pci/drivers/iwl/)
 * used for controlling the debug level.
 *
 * See the level definitions in iwl for details.
 */

static ssize_t show_debug_level(struct device_driver *d, char *buf)
{
	return sprintf(buf, "0x%08X\n", iwl_debug_level);
}
static ssize_t store_debug_level(struct device_driver *d,
				 const char *buf, size_t count)
{
	char *p = (char *)buf;
	u32 val;

	val = simple_strtoul(p, &p, 0);
	if (p == buf)
		printk(KERN_INFO DRV_NAME
		       ": %s is not in hex or decimal form.\n", buf);
	else
		iwl_debug_level = val;

	return strnlen(buf, count);
}

static DRIVER_ATTR(debug_level, S_IWUSR | S_IRUGO,
		   show_debug_level, store_debug_level);

#endif /* CONFIG_IWLWIFI_DEBUG */


static ssize_t show_temperature(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	return sprintf(buf, "%d\n", iwl4965_hw_get_temperature(priv));
}

static DEVICE_ATTR(temperature, S_IRUGO, show_temperature, NULL);

static ssize_t show_rs_window(struct device *d,
			      struct device_attribute *attr,
			      char *buf)
{
	struct iwl_priv *priv = d->driver_data;
	return iwl4965_fill_rs_info(priv->hw, buf, IWL_AP_ID);
}
static DEVICE_ATTR(rs_window, S_IRUGO, show_rs_window, NULL);

static ssize_t show_tx_power(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	return sprintf(buf, "%d\n", priv->user_txpower_limit);
}

static ssize_t store_tx_power(struct device *d,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	char *p = (char *)buf;
	u32 val;

	val = simple_strtoul(p, &p, 10);
	if (p == buf)
		printk(KERN_INFO DRV_NAME
		       ": %s is not in decimal form.\n", buf);
	else
		iwl4965_hw_reg_set_txpower(priv, val);

	return count;
}

static DEVICE_ATTR(tx_power, S_IWUSR | S_IRUGO, show_tx_power, store_tx_power);

static ssize_t show_flags(struct device *d,
			  struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;

	return sprintf(buf, "0x%04X\n", priv->active_rxon.flags);
}

static ssize_t store_flags(struct device *d,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	u32 flags = simple_strtoul(buf, NULL, 0);

	mutex_lock(&priv->mutex);
	if (le32_to_cpu(priv->staging_rxon.flags) != flags) {
		/* Cancel any currently running scans... */
		if (iwl4965_scan_cancel_timeout(priv, 100))
			IWL_WARNING("Could not cancel scan.\n");
		else {
			IWL_DEBUG_INFO("Committing rxon.flags = 0x%04X\n",
				       flags);
			priv->staging_rxon.flags = cpu_to_le32(flags);
			iwl4965_commit_rxon(priv);
		}
	}
	mutex_unlock(&priv->mutex);

	return count;
}

static DEVICE_ATTR(flags, S_IWUSR | S_IRUGO, show_flags, store_flags);

static ssize_t show_filter_flags(struct device *d,
				 struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;

	return sprintf(buf, "0x%04X\n",
		le32_to_cpu(priv->active_rxon.filter_flags));
}

static ssize_t store_filter_flags(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	u32 filter_flags = simple_strtoul(buf, NULL, 0);

	mutex_lock(&priv->mutex);
	if (le32_to_cpu(priv->staging_rxon.filter_flags) != filter_flags) {
		/* Cancel any currently running scans... */
		if (iwl4965_scan_cancel_timeout(priv, 100))
			IWL_WARNING("Could not cancel scan.\n");
		else {
			IWL_DEBUG_INFO("Committing rxon.filter_flags = "
				       "0x%04X\n", filter_flags);
			priv->staging_rxon.filter_flags =
				cpu_to_le32(filter_flags);
			iwl4965_commit_rxon(priv);
		}
	}
	mutex_unlock(&priv->mutex);

	return count;
}

static DEVICE_ATTR(filter_flags, S_IWUSR | S_IRUGO, show_filter_flags,
		   store_filter_flags);

#ifdef CONFIG_IWL4965_SPECTRUM_MEASUREMENT

static ssize_t show_measurement(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	struct iwl4965_spectrum_notification measure_report;
	u32 size = sizeof(measure_report), len = 0, ofs = 0;
	u8 *data = (u8 *) & measure_report;
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

static ssize_t store_measurement(struct device *d,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	struct ieee80211_measurement_params params = {
		.channel = le16_to_cpu(priv->active_rxon.channel),
		.start_time = cpu_to_le64(priv->last_tsf),
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

	IWL_DEBUG_INFO("Invoking measurement of type %d on "
		       "channel %d (for '%s')\n", type, params.channel, buf);
	iwl4965_get_measurement(priv, &params, type);

	return count;
}

static DEVICE_ATTR(measurement, S_IRUSR | S_IWUSR,
		   show_measurement, store_measurement);
#endif /* CONFIG_IWL4965_SPECTRUM_MEASUREMENT */

static ssize_t store_retry_rate(struct device *d,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	priv->retry_rate = simple_strtoul(buf, NULL, 0);
	if (priv->retry_rate <= 0)
		priv->retry_rate = 1;

	return count;
}

static ssize_t show_retry_rate(struct device *d,
			       struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	return sprintf(buf, "%d", priv->retry_rate);
}

static DEVICE_ATTR(retry_rate, S_IWUSR | S_IRUSR, show_retry_rate,
		   store_retry_rate);

static ssize_t store_power_level(struct device *d,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	int rc;
	int mode;

	mode = simple_strtoul(buf, NULL, 0);
	mutex_lock(&priv->mutex);

	if (!iwl_is_ready(priv)) {
		rc = -EAGAIN;
		goto out;
	}

	if ((mode < 1) || (mode > IWL_POWER_LIMIT) || (mode == IWL_POWER_AC))
		mode = IWL_POWER_AC;
	else
		mode |= IWL_POWER_ENABLED;

	if (mode != priv->power_mode) {
		rc = iwl4965_send_power_mode(priv, IWL_POWER_LEVEL(mode));
		if (rc) {
			IWL_DEBUG_MAC80211("failed setting power mode.\n");
			goto out;
		}
		priv->power_mode = mode;
	}

	rc = count;

 out:
	mutex_unlock(&priv->mutex);
	return rc;
}

#define MAX_WX_STRING 80

/* Values are in microsecond */
static const s32 timeout_duration[] = {
	350000,
	250000,
	75000,
	37000,
	25000,
};
static const s32 period_duration[] = {
	400000,
	700000,
	1000000,
	1000000,
	1000000
};

static ssize_t show_power_level(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	int level = IWL_POWER_LEVEL(priv->power_mode);
	char *p = buf;

	p += sprintf(p, "%d ", level);
	switch (level) {
	case IWL_POWER_MODE_CAM:
	case IWL_POWER_AC:
		p += sprintf(p, "(AC)");
		break;
	case IWL_POWER_BATTERY:
		p += sprintf(p, "(BATTERY)");
		break;
	default:
		p += sprintf(p,
			     "(Timeout %dms, Period %dms)",
			     timeout_duration[level - 1] / 1000,
			     period_duration[level - 1] / 1000);
	}

	if (!(priv->power_mode & IWL_POWER_ENABLED))
		p += sprintf(p, " OFF\n");
	else
		p += sprintf(p, " \n");

	return (p - buf + 1);

}

static DEVICE_ATTR(power_level, S_IWUSR | S_IRUSR, show_power_level,
		   store_power_level);

static ssize_t show_channels(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	/* all this shit doesn't belong into sysfs anyway */
	return 0;
}

static DEVICE_ATTR(channels, S_IRUSR, show_channels, NULL);

static ssize_t show_statistics(struct device *d,
			       struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	u32 size = sizeof(struct iwl4965_notif_statistics);
	u32 len = 0, ofs = 0;
	u8 *data = (u8 *) & priv->statistics;
	int rc = 0;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	mutex_lock(&priv->mutex);
	rc = iwl_send_statistics_request(priv, 0);
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

static ssize_t show_antenna(struct device *d,
			    struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	return sprintf(buf, "%d\n", priv->antenna);
}

static ssize_t store_antenna(struct device *d,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int ant;
	struct iwl_priv *priv = dev_get_drvdata(d);

	if (count == 0)
		return 0;

	if (sscanf(buf, "%1i", &ant) != 1) {
		IWL_DEBUG_INFO("not in hex or decimal form.\n");
		return count;
	}

	if ((ant >= 0) && (ant <= 2)) {
		IWL_DEBUG_INFO("Setting antenna select to %d.\n", ant);
		priv->antenna = (enum iwl4965_antenna)ant;
	} else
		IWL_DEBUG_INFO("Bad antenna select value %d.\n", ant);


	return count;
}

static DEVICE_ATTR(antenna, S_IWUSR | S_IRUGO, show_antenna, store_antenna);

static ssize_t show_status(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	if (!iwl_is_alive(priv))
		return -EAGAIN;
	return sprintf(buf, "0x%08x\n", (int)priv->status);
}

static DEVICE_ATTR(status, S_IRUGO, show_status, NULL);

static ssize_t dump_error_log(struct device *d,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	char *p = (char *)buf;

	if (p[0] == '1')
		iwl4965_dump_nic_error_log((struct iwl_priv *)d->driver_data);

	return strnlen(buf, count);
}

static DEVICE_ATTR(dump_errors, S_IWUSR, NULL, dump_error_log);

static ssize_t dump_event_log(struct device *d,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	char *p = (char *)buf;

	if (p[0] == '1')
		iwl4965_dump_nic_event_log((struct iwl_priv *)d->driver_data);

	return strnlen(buf, count);
}

static DEVICE_ATTR(dump_events, S_IWUSR, NULL, dump_event_log);

/*****************************************************************************
 *
 * driver setup and teardown
 *
 *****************************************************************************/

static void iwl4965_setup_deferred_work(struct iwl_priv *priv)
{
	priv->workqueue = create_workqueue(DRV_NAME);

	init_waitqueue_head(&priv->wait_command_queue);

	INIT_WORK(&priv->up, iwl4965_bg_up);
	INIT_WORK(&priv->restart, iwl4965_bg_restart);
	INIT_WORK(&priv->rx_replenish, iwl4965_bg_rx_replenish);
	INIT_WORK(&priv->scan_completed, iwl4965_bg_scan_completed);
	INIT_WORK(&priv->request_scan, iwl4965_bg_request_scan);
	INIT_WORK(&priv->abort_scan, iwl4965_bg_abort_scan);
	INIT_WORK(&priv->rf_kill, iwl4965_bg_rf_kill);
	INIT_WORK(&priv->beacon_update, iwl4965_bg_beacon_update);
	INIT_DELAYED_WORK(&priv->post_associate, iwl4965_bg_post_associate);
	INIT_DELAYED_WORK(&priv->init_alive_start, iwl4965_bg_init_alive_start);
	INIT_DELAYED_WORK(&priv->alive_start, iwl4965_bg_alive_start);
	INIT_DELAYED_WORK(&priv->scan_check, iwl4965_bg_scan_check);

	iwl4965_hw_setup_deferred_work(priv);

	tasklet_init(&priv->irq_tasklet, (void (*)(unsigned long))
		     iwl4965_irq_tasklet, (unsigned long)priv);
}

static void iwl4965_cancel_deferred_work(struct iwl_priv *priv)
{
	iwl4965_hw_cancel_deferred_work(priv);

	cancel_delayed_work_sync(&priv->init_alive_start);
	cancel_delayed_work(&priv->scan_check);
	cancel_delayed_work(&priv->alive_start);
	cancel_delayed_work(&priv->post_associate);
	cancel_work_sync(&priv->beacon_update);
}

static struct attribute *iwl4965_sysfs_entries[] = {
	&dev_attr_antenna.attr,
	&dev_attr_channels.attr,
	&dev_attr_dump_errors.attr,
	&dev_attr_dump_events.attr,
	&dev_attr_flags.attr,
	&dev_attr_filter_flags.attr,
#ifdef CONFIG_IWL4965_SPECTRUM_MEASUREMENT
	&dev_attr_measurement.attr,
#endif
	&dev_attr_power_level.attr,
	&dev_attr_retry_rate.attr,
	&dev_attr_rs_window.attr,
	&dev_attr_statistics.attr,
	&dev_attr_status.attr,
	&dev_attr_temperature.attr,
	&dev_attr_tx_power.attr,

	NULL
};

static struct attribute_group iwl4965_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = iwl4965_sysfs_entries,
};

static struct ieee80211_ops iwl4965_hw_ops = {
	.tx = iwl4965_mac_tx,
	.start = iwl4965_mac_start,
	.stop = iwl4965_mac_stop,
	.add_interface = iwl4965_mac_add_interface,
	.remove_interface = iwl4965_mac_remove_interface,
	.config = iwl4965_mac_config,
	.config_interface = iwl4965_mac_config_interface,
	.configure_filter = iwl4965_configure_filter,
	.set_key = iwl4965_mac_set_key,
	.update_tkip_key = iwl4965_mac_update_tkip_key,
	.get_stats = iwl4965_mac_get_stats,
	.get_tx_stats = iwl4965_mac_get_tx_stats,
	.conf_tx = iwl4965_mac_conf_tx,
	.get_tsf = iwl4965_mac_get_tsf,
	.reset_tsf = iwl4965_mac_reset_tsf,
	.beacon_update = iwl4965_mac_beacon_update,
	.bss_info_changed = iwl4965_bss_info_changed,
#ifdef CONFIG_IWL4965_HT
	.ampdu_action = iwl4965_mac_ampdu_action,
#endif  /* CONFIG_IWL4965_HT */
	.hw_scan = iwl4965_mac_hw_scan
};

static int iwl4965_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = 0;
	struct iwl_priv *priv;
	struct ieee80211_hw *hw;
	struct iwl_cfg *cfg = (struct iwl_cfg *)(ent->driver_data);
	unsigned long flags;
	DECLARE_MAC_BUF(mac);

	/************************
	 * 1. Allocating HW data
	 ************************/

	/* Disabling hardware scan means that mac80211 will perform scans
	 * "the hard way", rather than using device's scan. */
	if (cfg->mod_params->disable_hw_scan) {
		IWL_DEBUG_INFO("Disabling hw_scan\n");
		iwl4965_hw_ops.hw_scan = NULL;
	}

	hw = iwl_alloc_all(cfg, &iwl4965_hw_ops);
	if (!hw) {
		err = -ENOMEM;
		goto out;
	}
	priv = hw->priv;
	/* At this point both hw and priv are allocated. */

	SET_IEEE80211_DEV(hw, &pdev->dev);

	IWL_DEBUG_INFO("*** LOAD DRIVER ***\n");
	priv->cfg = cfg;
	priv->pci_dev = pdev;

#ifdef CONFIG_IWLWIFI_DEBUG
	iwl_debug_level = priv->cfg->mod_params->debug;
	atomic_set(&priv->restrict_refcnt, 0);
#endif

	/**************************
	 * 2. Initializing PCI bus
	 **************************/
	if (pci_enable_device(pdev)) {
		err = -ENODEV;
		goto out_ieee80211_free_hw;
	}

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
		if (err) {
			printk(KERN_WARNING DRV_NAME
				": No suitable DMA available.\n");
			goto out_pci_disable_device;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err)
		goto out_pci_disable_device;

	pci_set_drvdata(pdev, priv);

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config_byte(pdev, 0x41, 0x00);

	/***********************
	 * 3. Read REV register
	 ***********************/
	priv->hw_base = pci_iomap(pdev, 0, 0);
	if (!priv->hw_base) {
		err = -ENODEV;
		goto out_pci_release_regions;
	}

	IWL_DEBUG_INFO("pci_resource_len = 0x%08llx\n",
		(unsigned long long) pci_resource_len(pdev, 0));
	IWL_DEBUG_INFO("pci_resource_base = %p\n", priv->hw_base);

	printk(KERN_INFO DRV_NAME
		": Detected Intel Wireless WiFi Link %s\n", priv->cfg->name);

	/*****************
	 * 4. Read EEPROM
	 *****************/
	/* nic init */
	iwl_set_bit(priv, CSR_GIO_CHICKEN_BITS,
		CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	iwl_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
	err = iwl_poll_bit(priv, CSR_GP_CNTRL,
		CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
		CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (err < 0) {
		IWL_DEBUG_INFO("Failed to init the card\n");
		goto out_iounmap;
	}
	/* Read the EEPROM */
	err = iwl_eeprom_init(priv);
	if (err) {
		IWL_ERROR("Unable to init EEPROM\n");
		goto out_iounmap;
	}
	/* MAC Address location in EEPROM same for 3945/4965 */
	iwl_eeprom_get_mac(priv, priv->mac_addr);
	IWL_DEBUG_INFO("MAC address: %s\n", print_mac(mac, priv->mac_addr));
	SET_IEEE80211_PERM_ADDR(priv->hw, priv->mac_addr);

	/************************
	 * 5. Setup HW constants
	 ************************/
	/* Device-specific setup */
	if (priv->cfg->ops->lib->set_hw_params(priv)) {
		IWL_ERROR("failed to set hw parameters\n");
		goto out_iounmap;
	}

	/*******************
	 * 6. Setup hw/priv
	 *******************/

	err = iwl_setup(priv);
	if (err)
		goto out_unset_hw_params;
	/* At this point both hw and priv are initialized. */

	/**********************************
	 * 7. Initialize module parameters
	 **********************************/

	/* Disable radio (SW RF KILL) via parameter when loading driver */
	if (priv->cfg->mod_params->disable) {
		set_bit(STATUS_RF_KILL_SW, &priv->status);
		IWL_DEBUG_INFO("Radio disabled.\n");
	}

	if (priv->cfg->mod_params->enable_qos)
		priv->qos_data.qos_enable = 1;

	/********************
	 * 8. Setup services
	 ********************/
	spin_lock_irqsave(&priv->lock, flags);
	iwl4965_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	err = sysfs_create_group(&pdev->dev.kobj, &iwl4965_attribute_group);
	if (err) {
		IWL_ERROR("failed to create sysfs device attributes\n");
		goto out_unset_hw_params;
	}

	err = iwl_dbgfs_register(priv, DRV_NAME);
	if (err) {
		IWL_ERROR("failed to create debugfs files\n");
		goto out_remove_sysfs;
	}

	iwl4965_setup_deferred_work(priv);
	iwl4965_setup_rx_handlers(priv);

	/********************
	 * 9. Conclude
	 ********************/
	pci_save_state(pdev);
	pci_disable_device(pdev);

	/* notify iwlcore to init */
	iwlcore_low_level_notify(priv, IWLCORE_INIT_EVT);
	return 0;

 out_remove_sysfs:
	sysfs_remove_group(&pdev->dev.kobj, &iwl4965_attribute_group);
 out_unset_hw_params:
	iwl4965_unset_hw_params(priv);
 out_iounmap:
	pci_iounmap(pdev, priv->hw_base);
 out_pci_release_regions:
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
 out_pci_disable_device:
	pci_disable_device(pdev);
 out_ieee80211_free_hw:
	ieee80211_free_hw(priv->hw);
 out:
	return err;
}

static void __devexit iwl4965_pci_remove(struct pci_dev *pdev)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);
	struct list_head *p, *q;
	int i;
	unsigned long flags;

	if (!priv)
		return;

	IWL_DEBUG_INFO("*** UNLOAD DRIVER ***\n");

	if (priv->mac80211_registered) {
		ieee80211_unregister_hw(priv->hw);
		priv->mac80211_registered = 0;
	}

	set_bit(STATUS_EXIT_PENDING, &priv->status);

	iwl4965_down(priv);

	/* make sure we flush any pending irq or
	 * tasklet for the driver
	 */
	spin_lock_irqsave(&priv->lock, flags);
	iwl4965_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_synchronize_irq(priv);

	/* Free MAC hash list for ADHOC */
	for (i = 0; i < IWL_IBSS_MAC_HASH_SIZE; i++) {
		list_for_each_safe(p, q, &priv->ibss_mac_hash[i]) {
			list_del(p);
			kfree(list_entry(p, struct iwl4965_ibss_seq, list));
		}
	}

	iwlcore_low_level_notify(priv, IWLCORE_REMOVE_EVT);
	iwl_dbgfs_unregister(priv);
	sysfs_remove_group(&pdev->dev.kobj, &iwl4965_attribute_group);

	iwl4965_dealloc_ucode_pci(priv);

	if (priv->rxq.bd)
		iwl4965_rx_queue_free(priv, &priv->rxq);
	iwl4965_hw_txq_ctx_free(priv);

	iwl4965_unset_hw_params(priv);
	iwlcore_clear_stations_table(priv);


	/*netif_stop_queue(dev); */
	flush_workqueue(priv->workqueue);

	/* ieee80211_unregister_hw calls iwl4965_mac_stop, which flushes
	 * priv->workqueue... so we can't take down the workqueue
	 * until now... */
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;

	pci_iounmap(pdev, priv->hw_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	iwl_free_channel_map(priv);
	iwl4965_free_geos(priv);

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	ieee80211_free_hw(priv->hw);
}

#ifdef CONFIG_PM

static int iwl4965_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);

	if (priv->is_open) {
		set_bit(STATUS_IN_SUSPEND, &priv->status);
		iwl4965_mac_stop(priv->hw);
		priv->is_open = 1;
	}

	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int iwl4965_pci_resume(struct pci_dev *pdev)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);

	pci_set_power_state(pdev, PCI_D0);

	if (priv->is_open)
		iwl4965_mac_start(priv->hw);

	clear_bit(STATUS_IN_SUSPEND, &priv->status);
	return 0;
}

#endif /* CONFIG_PM */

/*****************************************************************************
 *
 * driver and module entry point
 *
 *****************************************************************************/

/* Hardware specific file defines the PCI IDs table for that hardware module */
static struct pci_device_id iwl_hw_card_ids[] = {
	{IWL_PCI_DEVICE(0x4229, PCI_ANY_ID, iwl4965_agn_cfg)},
	{IWL_PCI_DEVICE(0x4230, PCI_ANY_ID, iwl4965_agn_cfg)},
	{0}
};
MODULE_DEVICE_TABLE(pci, iwl_hw_card_ids);

static struct pci_driver iwl_driver = {
	.name = DRV_NAME,
	.id_table = iwl_hw_card_ids,
	.probe = iwl4965_pci_probe,
	.remove = __devexit_p(iwl4965_pci_remove),
#ifdef CONFIG_PM
	.suspend = iwl4965_pci_suspend,
	.resume = iwl4965_pci_resume,
#endif
};

static int __init iwl4965_init(void)
{

	int ret;
	printk(KERN_INFO DRV_NAME ": " DRV_DESCRIPTION ", " DRV_VERSION "\n");
	printk(KERN_INFO DRV_NAME ": " DRV_COPYRIGHT "\n");

	ret = iwl4965_rate_control_register();
	if (ret) {
		IWL_ERROR("Unable to register rate control algorithm: %d\n", ret);
		return ret;
	}

	ret = pci_register_driver(&iwl_driver);
	if (ret) {
		IWL_ERROR("Unable to initialize PCI module\n");
		goto error_register;
	}
#ifdef CONFIG_IWLWIFI_DEBUG
	ret = driver_create_file(&iwl_driver.driver, &driver_attr_debug_level);
	if (ret) {
		IWL_ERROR("Unable to create driver sysfs file\n");
		goto error_debug;
	}
#endif

	return ret;

#ifdef CONFIG_IWLWIFI_DEBUG
error_debug:
	pci_unregister_driver(&iwl_driver);
#endif
error_register:
	iwl4965_rate_control_unregister();
	return ret;
}

static void __exit iwl4965_exit(void)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	driver_remove_file(&iwl_driver.driver, &driver_attr_debug_level);
#endif
	pci_unregister_driver(&iwl_driver);
	iwl4965_rate_control_unregister();
}

module_exit(iwl4965_exit);
module_init(iwl4965_init);
