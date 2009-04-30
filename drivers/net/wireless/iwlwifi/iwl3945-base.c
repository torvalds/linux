/******************************************************************************
 *
 * Copyright(c) 2003 - 2009 Intel Corporation. All rights reserved.
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
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include <net/ieee80211_radiotap.h>
#include <net/lib80211.h>
#include <net/mac80211.h>

#include <asm/div64.h>

#define DRV_NAME	"iwl3945"

#include "iwl-fh.h"
#include "iwl-3945-fh.h"
#include "iwl-commands.h"
#include "iwl-sta.h"
#include "iwl-3945.h"
#include "iwl-helpers.h"
#include "iwl-core.h"
#include "iwl-dev.h"

/*
 * module name, copyright, version, etc.
 */

#define DRV_DESCRIPTION	\
"Intel(R) PRO/Wireless 3945ABG/BG Network Connection driver for Linux"

#ifdef CONFIG_IWLWIFI_DEBUG
#define VD "d"
#else
#define VD
#endif

#ifdef CONFIG_IWL3945_SPECTRUM_MEASUREMENT
#define VS "s"
#else
#define VS
#endif

#define IWL39_VERSION "1.2.26k" VD VS
#define DRV_COPYRIGHT	"Copyright(c) 2003-2009 Intel Corporation"
#define DRV_AUTHOR     "<ilw@linux.intel.com>"
#define DRV_VERSION     IWL39_VERSION


MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");

 /* module parameters */
struct iwl_mod_params iwl3945_mod_params = {
	.num_of_queues = IWL39_MAX_NUM_QUEUES,
	.sw_crypto = 1,
	.restart_fw = 1,
	/* the rest are 0 by default */
};

/*************** STATION TABLE MANAGEMENT ****
 * mac80211 should be examined to determine if sta_info is duplicating
 * the functionality provided here
 */

/**************************************************************/
#if 0 /* temporary disable till we add real remove station */
/**
 * iwl3945_remove_station - Remove driver's knowledge of station.
 *
 * NOTE:  This does not remove station from device's station table.
 */
static u8 iwl3945_remove_station(struct iwl_priv *priv, const u8 *addr, int is_ap)
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
			if (priv->stations_39[i].used &&
			    !compare_ether_addr(priv->stations_39[i].sta.sta.addr,
						addr)) {
				index = i;
				break;
			}

	if (unlikely(index == IWL_INVALID_STATION))
		goto out;

	if (priv->stations_39[index].used) {
		priv->stations_39[index].used = 0;
		priv->num_stations--;
	}

	BUG_ON(priv->num_stations < 0);

out:
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return 0;
}
#endif

/**
 * iwl3945_clear_stations_table - Clear the driver's station table
 *
 * NOTE:  This does not clear or otherwise alter the device's station table.
 */
static void iwl3945_clear_stations_table(struct iwl_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->num_stations = 0;
	memset(priv->stations_39, 0, sizeof(priv->stations_39));

	spin_unlock_irqrestore(&priv->sta_lock, flags);
}

/**
 * iwl3945_add_station - Add station to station tables in driver and device
 */
u8 iwl3945_add_station(struct iwl_priv *priv, const u8 *addr, int is_ap, u8 flags)
{
	int i;
	int index = IWL_INVALID_STATION;
	struct iwl3945_station_entry *station;
	unsigned long flags_spin;
	u8 rate;

	spin_lock_irqsave(&priv->sta_lock, flags_spin);
	if (is_ap)
		index = IWL_AP_ID;
	else if (is_broadcast_ether_addr(addr))
		index = priv->hw_params.bcast_sta_id;
	else
		for (i = IWL_STA_ID; i < priv->hw_params.max_stations; i++) {
			if (!compare_ether_addr(priv->stations_39[i].sta.sta.addr,
						addr)) {
				index = i;
				break;
			}

			if (!priv->stations_39[i].used &&
			    index == IWL_INVALID_STATION)
				index = i;
		}

	/* These two conditions has the same outcome but keep them separate
	  since they have different meaning */
	if (unlikely(index == IWL_INVALID_STATION)) {
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return index;
	}

	if (priv->stations_39[index].used &&
	   !compare_ether_addr(priv->stations_39[index].sta.sta.addr, addr)) {
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return index;
	}

	IWL_DEBUG_ASSOC(priv, "Add STA ID %d: %pM\n", index, addr);
	station = &priv->stations_39[index];
	station->used = 1;
	priv->num_stations++;

	/* Set up the REPLY_ADD_STA command to send to device */
	memset(&station->sta, 0, sizeof(struct iwl3945_addsta_cmd));
	memcpy(station->sta.sta.addr, addr, ETH_ALEN);
	station->sta.mode = 0;
	station->sta.sta.sta_id = index;
	station->sta.station_flags = 0;

	if (priv->band == IEEE80211_BAND_5GHZ)
		rate = IWL_RATE_6M_PLCP;
	else
		rate =	IWL_RATE_1M_PLCP;

	/* Turn on both antennas for the station... */
	station->sta.rate_n_flags =
			iwl3945_hw_set_rate_n_flags(rate, RATE_MCS_ANT_AB_MSK);

	spin_unlock_irqrestore(&priv->sta_lock, flags_spin);

	/* Add station to device's station table */
	iwl_send_add_sta(priv,
			 (struct iwl_addsta_cmd *)&station->sta, flags);
	return index;

}

static int iwl3945_send_rxon_assoc(struct iwl_priv *priv)
{
	int rc = 0;
	struct iwl_rx_packet *res = NULL;
	struct iwl3945_rxon_assoc_cmd rxon_assoc;
	struct iwl_host_cmd cmd = {
		.id = REPLY_RXON_ASSOC,
		.len = sizeof(rxon_assoc),
		.meta.flags = CMD_WANT_SKB,
		.data = &rxon_assoc,
	};
	const struct iwl_rxon_cmd *rxon1 = &priv->staging_rxon;
	const struct iwl_rxon_cmd *rxon2 = &priv->active_rxon;

	if ((rxon1->flags == rxon2->flags) &&
	    (rxon1->filter_flags == rxon2->filter_flags) &&
	    (rxon1->cck_basic_rates == rxon2->cck_basic_rates) &&
	    (rxon1->ofdm_basic_rates == rxon2->ofdm_basic_rates)) {
		IWL_DEBUG_INFO(priv, "Using current RXON_ASSOC.  Not resending.\n");
		return 0;
	}

	rxon_assoc.flags = priv->staging_rxon.flags;
	rxon_assoc.filter_flags = priv->staging_rxon.filter_flags;
	rxon_assoc.ofdm_basic_rates = priv->staging_rxon.ofdm_basic_rates;
	rxon_assoc.cck_basic_rates = priv->staging_rxon.cck_basic_rates;
	rxon_assoc.reserved = 0;

	rc = iwl_send_cmd_sync(priv, &cmd);
	if (rc)
		return rc;

	res = (struct iwl_rx_packet *)cmd.meta.u.skb->data;
	if (res->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from REPLY_RXON_ASSOC command\n");
		rc = -EIO;
	}

	priv->alloc_rxb_skb--;
	dev_kfree_skb_any(cmd.meta.u.skb);

	return rc;
}

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

/**
 * iwl3945_commit_rxon - commit staging_rxon to hardware
 *
 * The RXON command in staging_rxon is committed to the hardware and
 * the active_rxon structure is updated with the new data.  This
 * function correctly transitions out of the RXON_ASSOC_MSK state if
 * a HW tune is required based on the RXON structure changes.
 */
static int iwl3945_commit_rxon(struct iwl_priv *priv)
{
	/* cast away the const for active_rxon in this function */
	struct iwl3945_rxon_cmd *active_rxon = (void *)&priv->active_rxon;
	struct iwl3945_rxon_cmd *staging_rxon = (void *)&priv->staging_rxon;
	int rc = 0;
	bool new_assoc =
		!!(priv->staging_rxon.filter_flags & RXON_FILTER_ASSOC_MSK);

	if (!iwl_is_alive(priv))
		return -1;

	/* always get timestamp with Rx frame */
	staging_rxon->flags |= RXON_FLG_TSF2HOST_MSK;

	/* select antenna */
	staging_rxon->flags &=
	    ~(RXON_FLG_DIS_DIV_MSK | RXON_FLG_ANT_SEL_MSK);
	staging_rxon->flags |= iwl3945_get_antenna_flags(priv);

	rc = iwl_check_rxon_cmd(priv);
	if (rc) {
		IWL_ERR(priv, "Invalid RXON configuration.  Not committing.\n");
		return -EINVAL;
	}

	/* If we don't need to send a full RXON, we can use
	 * iwl3945_rxon_assoc_cmd which is used to reconfigure filter
	 * and other flags for the current radio configuration. */
	if (!iwl_full_rxon_required(priv)) {
		rc = iwl3945_send_rxon_assoc(priv);
		if (rc) {
			IWL_ERR(priv, "Error setting RXON_ASSOC "
				  "configuration (%d).\n", rc);
			return rc;
		}

		memcpy(active_rxon, staging_rxon, sizeof(*active_rxon));

		return 0;
	}

	/* If we are currently associated and the new config requires
	 * an RXON_ASSOC and the new config wants the associated mask enabled,
	 * we must clear the associated from the active configuration
	 * before we apply the new config */
	if (iwl_is_associated(priv) && new_assoc) {
		IWL_DEBUG_INFO(priv, "Toggling associated bit on current RXON\n");
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;

		/*
		 * reserved4 and 5 could have been filled by the iwlcore code.
		 * Let's clear them before pushing to the 3945.
		 */
		active_rxon->reserved4 = 0;
		active_rxon->reserved5 = 0;
		rc = iwl_send_cmd_pdu(priv, REPLY_RXON,
				      sizeof(struct iwl3945_rxon_cmd),
				      &priv->active_rxon);

		/* If the mask clearing failed then we set
		 * active_rxon back to what it was previously */
		if (rc) {
			active_rxon->filter_flags |= RXON_FILTER_ASSOC_MSK;
			IWL_ERR(priv, "Error clearing ASSOC_MSK on current "
				  "configuration (%d).\n", rc);
			return rc;
		}
	}

	IWL_DEBUG_INFO(priv, "Sending RXON\n"
		       "* with%s RXON_FILTER_ASSOC_MSK\n"
		       "* channel = %d\n"
		       "* bssid = %pM\n",
		       (new_assoc ? "" : "out"),
		       le16_to_cpu(staging_rxon->channel),
		       staging_rxon->bssid_addr);

	/*
	 * reserved4 and 5 could have been filled by the iwlcore code.
	 * Let's clear them before pushing to the 3945.
	 */
	staging_rxon->reserved4 = 0;
	staging_rxon->reserved5 = 0;

	iwl_set_rxon_hwcrypto(priv, !priv->hw_params.sw_crypto);

	/* Apply the new configuration */
	rc = iwl_send_cmd_pdu(priv, REPLY_RXON,
			      sizeof(struct iwl3945_rxon_cmd),
			      staging_rxon);
	if (rc) {
		IWL_ERR(priv, "Error setting new configuration (%d).\n", rc);
		return rc;
	}

	memcpy(active_rxon, staging_rxon, sizeof(*active_rxon));

	iwl3945_clear_stations_table(priv);

	/* If we issue a new RXON command which required a tune then we must
	 * send a new TXPOWER command or we won't be able to Tx any frames */
	rc = priv->cfg->ops->lib->send_tx_power(priv);
	if (rc) {
		IWL_ERR(priv, "Error setting Tx power (%d).\n", rc);
		return rc;
	}

	/* Add the broadcast address so we can send broadcast frames */
	if (iwl3945_add_station(priv, iwl_bcast_addr, 0, 0) ==
	    IWL_INVALID_STATION) {
		IWL_ERR(priv, "Error adding BROADCAST address for transmit.\n");
		return -EIO;
	}

	/* If we have set the ASSOC_MSK and we are in BSS mode then
	 * add the IWL_AP_ID to the station rate table */
	if (iwl_is_associated(priv) &&
	    (priv->iw_mode == NL80211_IFTYPE_STATION))
		if (iwl3945_add_station(priv, priv->active_rxon.bssid_addr,
					1, 0)
		    == IWL_INVALID_STATION) {
			IWL_ERR(priv, "Error adding AP address for transmit\n");
			return -EIO;
		}

	/* Init the hardware's rate fallback order based on the band */
	rc = iwl3945_init_hw_rate_table(priv);
	if (rc) {
		IWL_ERR(priv, "Error setting HW rate table: %02X\n", rc);
		return -EIO;
	}

	return 0;
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

	if (sta_id == priv->hw_params.bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->hw_key_idx = keyconf->keyidx;
	key_flags &= ~STA_KEY_FLG_INVALID;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations_39[sta_id].keyinfo.alg = keyconf->alg;
	priv->stations_39[sta_id].keyinfo.keylen = keyconf->keylen;
	memcpy(priv->stations_39[sta_id].keyinfo.key, keyconf->key,
	       keyconf->keylen);

	memcpy(priv->stations_39[sta_id].sta.key.key, keyconf->key,
	       keyconf->keylen);

	if ((priv->stations_39[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		priv->stations_39[sta_id].sta.key.key_offset =
				 iwl_get_free_ucode_key_index(priv);
	/* else, we are overriding an existing key => no need to allocated room
	* in uCode. */

	WARN(priv->stations_39[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	priv->stations_39[sta_id].sta.key.key_flags = key_flags;
	priv->stations_39[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations_39[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	IWL_DEBUG_INFO(priv, "hwcrypto: modify ucode station key info\n");

	ret = iwl_send_add_sta(priv,
		(struct iwl_addsta_cmd *)&priv->stations_39[sta_id].sta, CMD_ASYNC);

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

	spin_lock_irqsave(&priv->sta_lock, flags);
	memset(&priv->stations_39[sta_id].keyinfo, 0, sizeof(struct iwl3945_hw_key));
	memset(&priv->stations_39[sta_id].sta.key, 0,
		sizeof(struct iwl4965_keyinfo));
	priv->stations_39[sta_id].sta.key.key_flags = STA_KEY_FLG_NO_ENC;
	priv->stations_39[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations_39[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	IWL_DEBUG_INFO(priv, "hwcrypto: clear ucode station key info\n");
	iwl_send_add_sta(priv,
		(struct iwl_addsta_cmd *)&priv->stations_39[sta_id].sta, 0);
	return 0;
}

static int iwl3945_set_dynamic_key(struct iwl_priv *priv,
			struct ieee80211_key_conf *keyconf, u8 sta_id)
{
	int ret = 0;

	keyconf->hw_key_idx = HW_KEY_DYNAMIC;

	switch (keyconf->alg) {
	case ALG_CCMP:
		ret = iwl3945_set_ccmp_dynamic_key_info(priv, keyconf, sta_id);
		break;
	case ALG_TKIP:
		ret = iwl3945_set_tkip_dynamic_key_info(priv, keyconf, sta_id);
		break;
	case ALG_WEP:
		ret = iwl3945_set_wep_dynamic_key_info(priv, keyconf, sta_id);
		break;
	default:
		IWL_ERR(priv, "Unknown alg: %s alg = %d\n", __func__, keyconf->alg);
		ret = -EINVAL;
	}

	IWL_DEBUG_WEP(priv, "Set dynamic key: alg= %d len=%d idx=%d sta=%d ret=%d\n",
		      keyconf->alg, keyconf->keylen, keyconf->keyidx,
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
	if (key->alg == ALG_WEP)
		return -EOPNOTSUPP;

	IWL_ERR(priv, "Static key invalid: alg %d\n", key->alg);
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

	if (!iwl_is_associated(priv) || !priv->ibss_beacon ||
	    ((priv->iw_mode != NL80211_IFTYPE_ADHOC) &&
	     (priv->iw_mode != NL80211_IFTYPE_AP)))
		return 0;

	if (priv->ibss_beacon->len > left)
		return 0;

	memcpy(hdr, priv->ibss_beacon->data, priv->ibss_beacon->len);

	return priv->ibss_beacon->len;
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

	rate = iwl_rate_get_lowest_plcp(priv);

	frame_size = iwl3945_hw_get_beacon_cmd(priv, frame, rate);

	rc = iwl_send_cmd_pdu(priv, REPLY_TX_BEACON, frame_size,
			      &frame->u.cmd[0]);

	iwl3945_free_frame(priv, frame);

	return rc;
}

static void iwl3945_unset_hw_params(struct iwl_priv *priv)
{
	if (priv->shared_virt)
		pci_free_consistent(priv->pci_dev,
				    sizeof(struct iwl3945_shared),
				    priv->shared_virt,
				    priv->shared_phys);
}

#define MAX_UCODE_BEACON_INTERVAL	1024
#define INTEL_CONN_LISTEN_INTERVAL	cpu_to_le16(0xA)

static __le16 iwl3945_adjust_beacon_interval(u16 beacon_val)
{
	u16 new_val = 0;
	u16 beacon_factor = 0;

	beacon_factor =
	    (beacon_val + MAX_UCODE_BEACON_INTERVAL)
		/ MAX_UCODE_BEACON_INTERVAL;
	new_val = beacon_val / beacon_factor;

	return cpu_to_le16(new_val);
}

static void iwl3945_setup_rxon_timing(struct iwl_priv *priv)
{
	u64 interval_tm_unit;
	u64 tsf, result;
	unsigned long flags;
	struct ieee80211_conf *conf = NULL;
	u16 beacon_int = 0;

	conf = ieee80211_get_hw_conf(priv->hw);

	spin_lock_irqsave(&priv->lock, flags);
	priv->rxon_timing.timestamp = cpu_to_le64(priv->timestamp);
	priv->rxon_timing.listen_interval = INTEL_CONN_LISTEN_INTERVAL;

	tsf = priv->timestamp;

	beacon_int = priv->beacon_int;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (priv->iw_mode == NL80211_IFTYPE_STATION) {
		if (beacon_int == 0) {
			priv->rxon_timing.beacon_interval = cpu_to_le16(100);
			priv->rxon_timing.beacon_init_val = cpu_to_le32(102400);
		} else {
			priv->rxon_timing.beacon_interval =
				cpu_to_le16(beacon_int);
			priv->rxon_timing.beacon_interval =
			    iwl3945_adjust_beacon_interval(
				le16_to_cpu(priv->rxon_timing.beacon_interval));
		}

		priv->rxon_timing.atim_window = 0;
	} else {
		priv->rxon_timing.beacon_interval =
			iwl3945_adjust_beacon_interval(conf->beacon_int);
		/* TODO: we need to get atim_window from upper stack
		 * for now we set to 0 */
		priv->rxon_timing.atim_window = 0;
	}

	interval_tm_unit =
		(le16_to_cpu(priv->rxon_timing.beacon_interval) * 1024);
	result = do_div(tsf, interval_tm_unit);
	priv->rxon_timing.beacon_init_val =
	    cpu_to_le32((u32) ((u64) interval_tm_unit - result));

	IWL_DEBUG_ASSOC(priv,
		"beacon interval %d beacon timer %d beacon tim %d\n",
		le16_to_cpu(priv->rxon_timing.beacon_interval),
		le32_to_cpu(priv->rxon_timing.beacon_init_val),
		le16_to_cpu(priv->rxon_timing.atim_window));
}

static int iwl3945_set_mode(struct iwl_priv *priv, int mode)
{
	if (mode == NL80211_IFTYPE_ADHOC) {
		const struct iwl_channel_info *ch_info;

		ch_info = iwl_get_channel_info(priv,
			priv->band,
			le16_to_cpu(priv->staging_rxon.channel));

		if (!ch_info || !is_channel_ibss(ch_info)) {
			IWL_ERR(priv, "channel %d not IBSS channel\n",
				  le16_to_cpu(priv->staging_rxon.channel));
			return -EINVAL;
		}
	}

	iwl_connection_init_rx_config(priv, mode);

	iwl3945_clear_stations_table(priv);

	/* don't commit rxon if rf-kill is on*/
	if (!iwl_is_ready_rf(priv))
		return -EAGAIN;

	cancel_delayed_work(&priv->scan_check);
	if (iwl_scan_cancel_timeout(priv, 100)) {
		IWL_WARN(priv, "Aborted scan still in progress after 100ms\n");
		IWL_DEBUG_MAC80211(priv, "leaving - scan abort failed.\n");
		return -EAGAIN;
	}

	iwl3945_commit_rxon(priv);

	return 0;
}

static void iwl3945_build_tx_cmd_hwcrypto(struct iwl_priv *priv,
				      struct ieee80211_tx_info *info,
				      struct iwl_cmd *cmd,
				      struct sk_buff *skb_frag,
				      int sta_id)
{
	struct iwl3945_tx_cmd *tx = (struct iwl3945_tx_cmd *)cmd->cmd.payload;
	struct iwl3945_hw_key *keyinfo =
	    &priv->stations_39[sta_id].keyinfo;

	switch (keyinfo->alg) {
	case ALG_CCMP:
		tx->sec_ctl = TX_CMD_SEC_CCM;
		memcpy(tx->key, keyinfo->key, keyinfo->keylen);
		IWL_DEBUG_TX(priv, "tx_cmd with AES hwcrypto\n");
		break;

	case ALG_TKIP:
		break;

	case ALG_WEP:
		tx->sec_ctl = TX_CMD_SEC_WEP |
		    (info->control.hw_key->hw_key_idx & TX_CMD_SEC_MSK) << TX_CMD_SEC_SHIFT;

		if (keyinfo->keylen == 13)
			tx->sec_ctl |= TX_CMD_SEC_KEY128;

		memcpy(&tx->key[3], keyinfo->key, keyinfo->keylen);

		IWL_DEBUG_TX(priv, "Configuring packet for WEP encryption "
			     "with key %d\n", info->control.hw_key->hw_key_idx);
		break;

	default:
		IWL_ERR(priv, "Unknown encode alg %d\n", keyinfo->alg);
		break;
	}
}

/*
 * handle build REPLY_TX command notification.
 */
static void iwl3945_build_tx_cmd_basic(struct iwl_priv *priv,
				  struct iwl_cmd *cmd,
				  struct ieee80211_tx_info *info,
				  struct ieee80211_hdr *hdr, u8 std_id)
{
	struct iwl3945_tx_cmd *tx = (struct iwl3945_tx_cmd *)cmd->cmd.payload;
	__le32 tx_flags = tx->tx_flags;
	__le16 fc = hdr->frame_control;
	u8 rc_flags = info->control.rates[0].flags;

	tx->stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;
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

	tx->sta_id = std_id;
	if (ieee80211_has_morefrags(fc))
		tx_flags |= TX_CMD_FLG_MORE_FRAG_MSK;

	if (ieee80211_is_data_qos(fc)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		tx->tid_tspec = qc[0] & 0xf;
		tx_flags &= ~TX_CMD_FLG_SEQ_CTL_MSK;
	} else {
		tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
	}

	if (rc_flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		tx_flags |= TX_CMD_FLG_RTS_MSK;
		tx_flags &= ~TX_CMD_FLG_CTS_MSK;
	} else if (rc_flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		tx_flags &= ~TX_CMD_FLG_RTS_MSK;
		tx_flags |= TX_CMD_FLG_CTS_MSK;
	}

	if ((tx_flags & TX_CMD_FLG_RTS_MSK) || (tx_flags & TX_CMD_FLG_CTS_MSK))
		tx_flags |= TX_CMD_FLG_FULL_TXOP_PROT_MSK;

	tx_flags &= ~(TX_CMD_FLG_ANT_SEL_MSK);
	if (ieee80211_is_mgmt(fc)) {
		if (ieee80211_is_assoc_req(fc) || ieee80211_is_reassoc_req(fc))
			tx->timeout.pm_frame_timeout = cpu_to_le16(3);
		else
			tx->timeout.pm_frame_timeout = cpu_to_le16(2);
	} else {
		tx->timeout.pm_frame_timeout = 0;
#ifdef CONFIG_IWLWIFI_LEDS
		priv->rxtxpackets += le16_to_cpu(cmd->cmd.tx.len);
#endif
	}

	tx->driver_txop = 0;
	tx->tx_flags = tx_flags;
	tx->next_frame_len = 0;
}

/**
 * iwl3945_get_sta_id - Find station's index within station table
 */
static int iwl3945_get_sta_id(struct iwl_priv *priv, struct ieee80211_hdr *hdr)
{
	int sta_id;
	u16 fc = le16_to_cpu(hdr->frame_control);

	/* If this frame is broadcast or management, use broadcast station id */
	if (((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA) ||
	    is_multicast_ether_addr(hdr->addr1))
		return priv->hw_params.bcast_sta_id;

	switch (priv->iw_mode) {

	/* If we are a client station in a BSS network, use the special
	 * AP station entry (that's the only station we communicate with) */
	case NL80211_IFTYPE_STATION:
		return IWL_AP_ID;

	/* If we are an AP, then find the station, or use BCAST */
	case NL80211_IFTYPE_AP:
		sta_id = iwl3945_hw_find_station(priv, hdr->addr1);
		if (sta_id != IWL_INVALID_STATION)
			return sta_id;
		return priv->hw_params.bcast_sta_id;

	/* If this frame is going out to an IBSS network, find the station,
	 * or create a new station table entry */
	case NL80211_IFTYPE_ADHOC: {
		/* Create new station table entry */
		sta_id = iwl3945_hw_find_station(priv, hdr->addr1);
		if (sta_id != IWL_INVALID_STATION)
			return sta_id;

		sta_id = iwl3945_add_station(priv, hdr->addr1, 0, CMD_ASYNC);

		if (sta_id != IWL_INVALID_STATION)
			return sta_id;

		IWL_DEBUG_DROP(priv, "Station %pM not in station map. "
			       "Defaulting to broadcast...\n",
			       hdr->addr1);
		iwl_print_hex_dump(priv, IWL_DL_DROP, (u8 *) hdr, sizeof(*hdr));
		return priv->hw_params.bcast_sta_id;
	}
	/* If we are in monitor mode, use BCAST. This is required for
	 * packet injection. */
	case NL80211_IFTYPE_MONITOR:
		return priv->hw_params.bcast_sta_id;

	default:
		IWL_WARN(priv, "Unknown mode of operation: %d\n",
			priv->iw_mode);
		return priv->hw_params.bcast_sta_id;
	}
}

/*
 * start REPLY_TX command process
 */
static int iwl3945_tx_skb(struct iwl_priv *priv, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct iwl3945_tx_cmd *tx;
	struct iwl_tx_queue *txq = NULL;
	struct iwl_queue *q = NULL;
	struct iwl_cmd *out_cmd = NULL;
	dma_addr_t phys_addr;
	dma_addr_t txcmd_phys;
	int txq_id = skb_get_queue_mapping(skb);
	u16 len, idx, len_org, hdr_len; /* TODO: len_org is not used */
	u8 id;
	u8 unicast;
	u8 sta_id;
	u8 tid = 0;
	u16 seq_number = 0;
	__le16 fc;
	u8 wait_write_ptr = 0;
	u8 *qc = NULL;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&priv->lock, flags);
	if (iwl_is_rfkill(priv)) {
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

#ifdef CONFIG_IWLWIFI_DEBUG
	if (ieee80211_is_auth(fc))
		IWL_DEBUG_TX(priv, "Sending AUTH frame\n");
	else if (ieee80211_is_assoc_req(fc))
		IWL_DEBUG_TX(priv, "Sending ASSOC frame\n");
	else if (ieee80211_is_reassoc_req(fc))
		IWL_DEBUG_TX(priv, "Sending REASSOC frame\n");
#endif

	/* drop all data frame if we are not associated */
	if (ieee80211_is_data(fc) &&
	    (priv->iw_mode != NL80211_IFTYPE_MONITOR) && /* packet injection */
	    (!iwl_is_associated(priv) ||
	     ((priv->iw_mode == NL80211_IFTYPE_STATION) && !priv->assoc_id))) {
		IWL_DEBUG_DROP(priv, "Dropping - !iwl_is_associated\n");
		goto drop_unlock;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	hdr_len = ieee80211_hdrlen(fc);

	/* Find (or create) index into station table for destination station */
	sta_id = iwl3945_get_sta_id(priv, hdr);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_DEBUG_DROP(priv, "Dropping - INVALID STATION: %pM\n",
			       hdr->addr1);
		goto drop;
	}

	IWL_DEBUG_RATE(priv, "station Id %d\n", sta_id);

	if (ieee80211_is_data_qos(fc)) {
		qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
		seq_number = priv->stations_39[sta_id].tid[tid].seq_number &
				IEEE80211_SCTL_SEQ;
		hdr->seq_ctrl = cpu_to_le16(seq_number) |
			(hdr->seq_ctrl &
				cpu_to_le16(IEEE80211_SCTL_FRAG));
		seq_number += 0x10;
	}

	/* Descriptor for chosen Tx queue */
	txq = &priv->txq[txq_id];
	q = &txq->q;

	spin_lock_irqsave(&priv->lock, flags);

	idx = get_cmd_index(q, q->write_ptr, 0);

	/* Set up driver data for this TFD */
	memset(&(txq->txb[q->write_ptr]), 0, sizeof(struct iwl_tx_info));
	txq->txb[q->write_ptr].skb[0] = skb;

	/* Init first empty entry in queue's array of Tx/cmd buffers */
	out_cmd = txq->cmd[idx];
	tx = (struct iwl3945_tx_cmd *)out_cmd->cmd.payload;
	memset(&out_cmd->hdr, 0, sizeof(out_cmd->hdr));
	memset(tx, 0, sizeof(*tx));

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
	memcpy(tx->hdr, hdr, hdr_len);


	if (info->control.hw_key)
		iwl3945_build_tx_cmd_hwcrypto(priv, info, out_cmd, skb, sta_id);

	/* TODO need this for burst mode later on */
	iwl3945_build_tx_cmd_basic(priv, out_cmd, info, hdr, sta_id);

	/* set is_hcca to 0; it probably will never be implemented */
	iwl3945_hw_build_tx_cmd_rate(priv, out_cmd, info, hdr, sta_id, 0);

	/* Total # bytes to be transmitted */
	len = (u16)skb->len;
	tx->len = cpu_to_le16(len);


	tx->tx_flags &= ~TX_CMD_FLG_ANT_A_MSK;
	tx->tx_flags &= ~TX_CMD_FLG_ANT_B_MSK;

	if (!ieee80211_has_morefrags(hdr->frame_control)) {
		txq->need_update = 1;
		if (qc)
			priv->stations_39[sta_id].tid[tid].seq_number = seq_number;
	} else {
		wait_write_ptr = 1;
		txq->need_update = 0;
	}

	IWL_DEBUG_TX(priv, "sequence nr = 0X%x \n",
		     le16_to_cpu(out_cmd->hdr.sequence));
	IWL_DEBUG_TX(priv, "tx_flags = 0X%x \n", le32_to_cpu(tx->tx_flags));
	iwl_print_hex_dump(priv, IWL_DL_TX, tx, sizeof(*tx));
	iwl_print_hex_dump(priv, IWL_DL_TX, (u8 *)tx->hdr,
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

	len_org = len;
	len = (len + 3) & ~3;

	if (len_org != len)
		len_org = 1;
	else
		len_org = 0;

	/* Physical address of this Tx command's header (not MAC header!),
	 * within command buffer array. */
	txcmd_phys = pci_map_single(priv->pci_dev, &out_cmd->hdr,
				    len, PCI_DMA_TODEVICE);
	/* we do not map meta data ... so we can safely access address to
	 * provide to unmap command*/
	pci_unmap_addr_set(&out_cmd->meta, mapping, txcmd_phys);
	pci_unmap_len_set(&out_cmd->meta, len, len);

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
	q->write_ptr = iwl_queue_inc_wrap(q->write_ptr, q->n_bd);
	rc = iwl_txq_update_write_ptr(priv, txq);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (rc)
		return rc;

	if ((iwl_queue_space(q) < q->high_mark)
	    && priv->mac80211_registered) {
		if (wait_write_ptr) {
			spin_lock_irqsave(&priv->lock, flags);
			txq->need_update = 1;
			iwl_txq_update_write_ptr(priv, txq);
			spin_unlock_irqrestore(&priv->lock, flags);
		}

		iwl_stop_queue(priv, skb_get_queue_mapping(skb));
	}

	return 0;

drop_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
drop:
	return -1;
}

#ifdef CONFIG_IWL3945_SPECTRUM_MEASUREMENT

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

static u32 iwl3945_usecs_to_beacons(u32 usec, u32 beacon_interval)
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

static __le32 iwl3945_add_beacon_time(u32 base, u32 addon, u32 beacon_interval)
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

static int iwl3945_get_measurement(struct iwl_priv *priv,
			       struct ieee80211_measurement_params *params,
			       u8 type)
{
	struct iwl_spectrum_cmd spectrum;
	struct iwl_rx_packet *res;
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
		    iwl3945_usecs_to_beacons(
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
		    iwl3945_add_beacon_time(priv->last_beacon_time,
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

	res = (struct iwl_rx_packet *)cmd.meta.u.skb->data;
	if (res->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from REPLY_RX_ON_ASSOC command\n");
		rc = -EIO;
	}

	spectrum_resp_status = le16_to_cpu(res->u.spectrum.status);
	switch (spectrum_resp_status) {
	case 0:		/* Command will be handled */
		if (res->u.spectrum.id != 0xff) {
			IWL_DEBUG_INFO(priv, "Replaced existing measurement: %d\n",
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

static void iwl3945_rx_reply_alive(struct iwl_priv *priv,
			       struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (void *)rxb->skb->data;
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
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = (void *)rxb->skb->data;
#endif

	IWL_DEBUG_RX(priv, "Received REPLY_ADD_STA: 0x%02X\n", pkt->u.status);
	return;
}

static void iwl3945_bg_beacon_update(struct work_struct *work)
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

	iwl3945_send_beacon_cmd(priv);
}

static void iwl3945_rx_beacon_notif(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl3945_beacon_notif *beacon = &(pkt->u.beacon_status);
	u8 rate = beacon->beacon_notify_hdr.rate;

	IWL_DEBUG_RX(priv, "beacon status %x retries %d iss %d "
		"tsf %d %d rate %d\n",
		le32_to_cpu(beacon->beacon_notify_hdr.status) & TX_STATUS_MSK,
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
static void iwl3945_rx_card_state_notif(struct iwl_priv *priv,
				    struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (void *)rxb->skb->data;
	u32 flags = le32_to_cpu(pkt->u.card_state_notif.flags);
	unsigned long status = priv->status;

	IWL_DEBUG_RF_KILL(priv, "Card state received: HW:%s SW:%s\n",
			  (flags & HW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & SW_CARD_DISABLED) ? "Kill" : "On");

	iwl_write32(priv, CSR_UCODE_DRV_GP1_SET,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	if (flags & HW_CARD_DISABLED)
		set_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		clear_bit(STATUS_RF_KILL_HW, &priv->status);


	if (flags & SW_CARD_DISABLED)
		set_bit(STATUS_RF_KILL_SW, &priv->status);
	else
		clear_bit(STATUS_RF_KILL_SW, &priv->status);

	iwl_scan_cancel(priv);

	if ((test_bit(STATUS_RF_KILL_HW, &status) !=
	     test_bit(STATUS_RF_KILL_HW, &priv->status)) ||
	    (test_bit(STATUS_RF_KILL_SW, &status) !=
	     test_bit(STATUS_RF_KILL_SW, &priv->status)))
		queue_work(priv->workqueue, &priv->rf_kill);
	else
		wake_up_interruptible(&priv->wait_command_queue);
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
	priv->rx_handlers[REPLY_ERROR] = iwl_rx_reply_error;
	priv->rx_handlers[CHANNEL_SWITCH_NOTIFICATION] = iwl_rx_csa;
	priv->rx_handlers[PM_SLEEP_NOTIFICATION] = iwl_rx_pm_sleep_notif;
	priv->rx_handlers[PM_DEBUG_STATISTIC_NOTIFIC] =
	    iwl_rx_pm_debug_statistics_notif;
	priv->rx_handlers[BEACON_NOTIFICATION] = iwl3945_rx_beacon_notif;

	/*
	 * The same handler is used for both the REPLY to a discrete
	 * statistics request from the host as well as for the periodic
	 * statistics notifications (after received beacons) from the uCode.
	 */
	priv->rx_handlers[REPLY_STATISTICS_CMD] = iwl3945_hw_rx_statistics;
	priv->rx_handlers[STATISTICS_NOTIFICATION] = iwl3945_hw_rx_statistics;

	iwl_setup_spectrum_handlers(priv);
	iwl_setup_rx_scan_handlers(priv);
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
static int iwl3945_rx_queue_restock(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	struct list_head *element;
	struct iwl_rx_mem_buffer *rxb;
	unsigned long flags;
	int write, rc;

	spin_lock_irqsave(&rxq->lock, flags);
	write = rxq->write & ~0x7;
	while ((iwl_rx_queue_space(rxq) > 0) && (rxq->free_count)) {
		/* Get next free Rx buffer, remove from free list */
		element = rxq->rx_free.next;
		rxb = list_entry(element, struct iwl_rx_mem_buffer, list);
		list_del(element);

		/* Point to Rx buffer via next RBD in circular buffer */
		rxq->bd[rxq->write] = iwl3945_dma_addr2rbd_ptr(priv, rxb->real_dma_addr);
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
		rc = iwl_rx_queue_update_write_ptr(priv, rxq);
		if (rc)
			return rc;
	}

	return 0;
}

/**
 * iwl3945_rx_replenish - Move all used packet from rx_used to rx_free
 *
 * When moving to rx_free an SKB is allocated for the slot.
 *
 * Also restock the Rx queue via iwl3945_rx_queue_restock.
 * This is called as a scheduled work item (except for during initialization)
 */
static void iwl3945_rx_allocate(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	struct list_head *element;
	struct iwl_rx_mem_buffer *rxb;
	unsigned long flags;
	spin_lock_irqsave(&rxq->lock, flags);
	while (!list_empty(&rxq->rx_used)) {
		element = rxq->rx_used.next;
		rxb = list_entry(element, struct iwl_rx_mem_buffer, list);

		/* Alloc a new receive buffer */
		rxb->skb =
		    alloc_skb(priv->hw_params.rx_buf_size,
				__GFP_NOWARN | GFP_ATOMIC);
		if (!rxb->skb) {
			if (net_ratelimit())
				IWL_CRIT(priv, ": Can not allocate SKB buffers\n");
			/* We don't reschedule replenish work here -- we will
			 * call the restock method and if it still needs
			 * more buffers it will schedule replenish */
			break;
		}

		/* If radiotap head is required, reserve some headroom here.
		 * The physical head count is a variable rx_stats->phy_count.
		 * We reserve 4 bytes here. Plus these extra bytes, the
		 * headroom of the physical head should be enough for the
		 * radiotap head that iwl3945 supported. See iwl3945_rt.
		 */
		skb_reserve(rxb->skb, 4);

		priv->alloc_rxb_skb++;
		list_del(element);

		/* Get physical address of RB/SKB */
		rxb->real_dma_addr = pci_map_single(priv->pci_dev,
						rxb->skb->data,
						priv->hw_params.rx_buf_size,
						PCI_DMA_FROMDEVICE);
		list_add_tail(&rxb->list, &rxq->rx_free);
		rxq->free_count++;
	}
	spin_unlock_irqrestore(&rxq->lock, flags);
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
		if (rxq->pool[i].skb != NULL) {
			pci_unmap_single(priv->pci_dev,
					 rxq->pool[i].real_dma_addr,
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
EXPORT_SYMBOL(iwl3945_rx_queue_reset);

/*
 * this should be called while priv->lock is locked
 */
static void __iwl3945_rx_replenish(void *data)
{
	struct iwl_priv *priv = data;

	iwl3945_rx_allocate(priv);
	iwl3945_rx_queue_restock(priv);
}


void iwl3945_rx_replenish(void *data)
{
	struct iwl_priv *priv = data;
	unsigned long flags;

	iwl3945_rx_allocate(priv);

	spin_lock_irqsave(&priv->lock, flags);
	iwl3945_rx_queue_restock(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
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
		if (rxq->pool[i].skb != NULL) {
			pci_unmap_single(priv->pci_dev,
					 rxq->pool[i].real_dma_addr,
					 priv->hw_params.rx_buf_size,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb(rxq->pool[i].skb);
		}
	}

	pci_free_consistent(priv->pci_dev, 4 * RX_QUEUE_SIZE, rxq->bd,
			    rxq->dma_addr);
	pci_free_consistent(priv->pci_dev, sizeof(struct iwl_rb_status),
			    rxq->rb_stts, rxq->rb_stts_dma);
	rxq->bd = NULL;
	rxq->rb_stts  = NULL;
}
EXPORT_SYMBOL(iwl3945_rx_queue_free);


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

#define PERFECT_RSSI (-20) /* dBm */
#define WORST_RSSI (-95)   /* dBm */
#define RSSI_RANGE (PERFECT_RSSI - WORST_RSSI)

/* Calculate an indication of rx signal quality (a percentage, not dBm!).
 * See http://www.ces.clemson.edu/linux/signal_quality.shtml for info
 *   about formulas used below. */
int iwl3945_calc_sig_qual(int rssi_dbm, int noise_dbm)
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

	/* uCode's read index (stored in shared DRAM) indicates the last Rx
	 * buffer that the driver may process (last buffer filled by ucode). */
	r = le16_to_cpu(rxq->rb_stts->closed_rb_num) &  0x0FFF;
	i = rxq->read;

	if (iwl_rx_queue_space(rxq) > (RX_QUEUE_SIZE / 2))
		fill_rx = 1;
	/* Rx interrupt, but nothing sent from uCode */
	if (i == r)
		IWL_DEBUG(priv, IWL_DL_RX | IWL_DL_ISR, "r = %d, i = %d\n", r, i);

	while (i != r) {
		rxb = rxq->queue[i];

		/* If an RXB doesn't have a Rx queue slot associated with it,
		 * then a bug has been introduced in the queue refilling
		 * routines -- catch it here */
		BUG_ON(rxb == NULL);

		rxq->queue[i] = NULL;

		pci_unmap_single(priv->pci_dev, rxb->real_dma_addr,
				priv->hw_params.rx_buf_size,
				PCI_DMA_FROMDEVICE);
		pkt = (struct iwl_rx_packet *)rxb->skb->data;

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
			IWL_DEBUG(priv, IWL_DL_HCMD | IWL_DL_RX | IWL_DL_ISR,
				"r = %d, i = %d, %s, 0x%02x\n", r, i,
				get_cmd_string(pkt->hdr.cmd), pkt->hdr.cmd);
			priv->rx_handlers[pkt->hdr.cmd] (priv, rxb);
		} else {
			/* No handling needed */
			IWL_DEBUG(priv, IWL_DL_HCMD | IWL_DL_RX | IWL_DL_ISR,
				"r %d i %d No handler needed for %s, 0x%02x\n",
				r, i, get_cmd_string(pkt->hdr.cmd),
				pkt->hdr.cmd);
		}

		if (reclaim) {
			/* Invoke any callbacks, transfer the skb to caller, and
			 * fire off the (possibly) blocking iwl_send_cmd()
			 * as we reclaim the driver command queue */
			if (rxb && rxb->skb)
				iwl_tx_cmd_complete(priv, rxb);
			else
				IWL_WARN(priv, "Claim null rxb?\n");
		}

		/* For now we just don't re-use anything.  We can tweak this
		 * later to try and re-use notification packets and SKBs that
		 * fail to Rx correctly */
		if (rxb->skb != NULL) {
			priv->alloc_rxb_skb--;
			dev_kfree_skb_any(rxb->skb);
			rxb->skb = NULL;
		}

		spin_lock_irqsave(&rxq->lock, flags);
		list_add_tail(&rxb->list, &priv->rxq.rx_used);
		spin_unlock_irqrestore(&rxq->lock, flags);
		i = (i + 1) & RX_QUEUE_MASK;
		/* If there are a lot of unused frames,
		 * restock the Rx queue so ucode won't assert. */
		if (fill_rx) {
			count++;
			if (count >= 8) {
				priv->rxq.read = i;
				__iwl3945_rx_replenish(priv);
				count = 0;
			}
		}
	}

	/* Backtrack one entry */
	priv->rxq.read = i;
	iwl3945_rx_queue_restock(priv);
}

/* call this function to flush any scheduled tasklet */
static inline void iwl_synchronize_irq(struct iwl_priv *priv)
{
	/* wait to make sure we flush pending tasklet*/
	synchronize_irq(priv->pci_dev->irq);
	tasklet_kill(&priv->irq_tasklet);
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

static void iwl3945_dump_nic_error_log(struct iwl_priv *priv)
{
	u32 i;
	u32 desc, time, count, base, data1;
	u32 blink1, blink2, ilink1, ilink2;
	int rc;

	base = le32_to_cpu(priv->card_alive.error_event_table_ptr);

	if (!iwl3945_hw_valid_rtc_data_addr(base)) {
		IWL_ERR(priv, "Not valid error log pointer 0x%08X\n", base);
		return;
	}

	rc = iwl_grab_nic_access(priv);
	if (rc) {
		IWL_WARN(priv, "Can not read from adapter at this time.\n");
		return;
	}

	count = iwl_read_targ_mem(priv, base);

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
		desc = iwl_read_targ_mem(priv, base + i);
		time =
		    iwl_read_targ_mem(priv, base + i + 1 * sizeof(u32));
		blink1 =
		    iwl_read_targ_mem(priv, base + i + 2 * sizeof(u32));
		blink2 =
		    iwl_read_targ_mem(priv, base + i + 3 * sizeof(u32));
		ilink1 =
		    iwl_read_targ_mem(priv, base + i + 4 * sizeof(u32));
		ilink2 =
		    iwl_read_targ_mem(priv, base + i + 5 * sizeof(u32));
		data1 =
		    iwl_read_targ_mem(priv, base + i + 6 * sizeof(u32));

		IWL_ERR(priv,
			"%-13s (#%d) %010u 0x%05X 0x%05X 0x%05X 0x%05X %u\n\n",
			desc_lookup(desc), desc, time, blink1, blink2,
			ilink1, ilink2, data1);
	}

	iwl_release_nic_access(priv);

}

#define EVENT_START_OFFSET  (6 * sizeof(u32))

/**
 * iwl3945_print_event_log - Dump error event log to syslog
 *
 * NOTE: Must be called with iwl_grab_nic_access() already obtained!
 */
static void iwl3945_print_event_log(struct iwl_priv *priv, u32 start_idx,
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
		if (mode == 0) {
			/* data, ev */
			IWL_ERR(priv, "0x%08x\t%04u\n", time, ev);
		} else {
			data = iwl_read_targ_mem(priv, ptr);
			ptr += sizeof(u32);
			IWL_ERR(priv, "%010u\t0x%08x\t%04u\n", time, data, ev);
		}
	}
}

static void iwl3945_dump_nic_event_log(struct iwl_priv *priv)
{
	int rc;
	u32 base;       /* SRAM byte address of event log header */
	u32 capacity;   /* event log capacity in # entries */
	u32 mode;       /* 0 - no timestamp, 1 - timestamp recorded */
	u32 num_wraps;  /* # times uCode wrapped to top of log */
	u32 next_entry; /* index of next entry to be written by uCode */
	u32 size;       /* # entries that we'll print */

	base = le32_to_cpu(priv->card_alive.log_event_table_ptr);
	if (!iwl3945_hw_valid_rtc_data_addr(base)) {
		IWL_ERR(priv, "Invalid event log pointer 0x%08X\n", base);
		return;
	}

	rc = iwl_grab_nic_access(priv);
	if (rc) {
		IWL_WARN(priv, "Can not read from adapter at this time.\n");
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
		IWL_ERR(priv, "Start IWL Event Log Dump: nothing in log\n");
		iwl_release_nic_access(priv);
		return;
	}

	IWL_ERR(priv, "Start IWL Event Log Dump: display count %d, wraps %d\n",
		  size, num_wraps);

	/* if uCode has wrapped back to top of log, start at the oldest entry,
	 * i.e the next one that uCode would fill. */
	if (num_wraps)
		iwl3945_print_event_log(priv, next_entry,
				    capacity - next_entry, mode);

	/* (then/else) start at top of log */
	iwl3945_print_event_log(priv, 0, next_entry, mode);

	iwl_release_nic_access(priv);
}

static void iwl3945_error_recovery(struct iwl_priv *priv)
{
	unsigned long flags;

	memcpy(&priv->staging_rxon, &priv->recovery_rxon,
	       sizeof(priv->staging_rxon));
	priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwl3945_commit_rxon(priv);

	iwl3945_add_station(priv, priv->bssid, 1, 0);

	spin_lock_irqsave(&priv->lock, flags);
	priv->assoc_id = le16_to_cpu(priv->staging_rxon.assoc_id);
	priv->error_recovering = 0;
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void iwl3945_irq_tasklet(struct iwl_priv *priv)
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
	if (priv->debug_level & IWL_DL_ISR) {
		/* just for debug */
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		IWL_DEBUG_ISR(priv, "inta 0x%08x, enabled 0x%08x, fh 0x%08x\n",
			      inta, inta_mask, inta_fh);
	}
#endif

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
		IWL_ERR(priv, "Microcode HW error detected.  Restarting.\n");

		/* Tell the device to stop sending interrupts */
		iwl_disable_interrupts(priv);

		iwl_irq_handle_error(priv);

		handled |= CSR_INT_BIT_HW_ERR;

		spin_unlock_irqrestore(&priv->lock, flags);

		return;
	}

#ifdef CONFIG_IWLWIFI_DEBUG
	if (priv->debug_level & (IWL_DL_ISR)) {
		/* NIC fires this, but we don't use it, redundant with WAKEUP */
		if (inta & CSR_INT_BIT_SCD)
			IWL_DEBUG_ISR(priv, "Scheduler finished to transmit "
				      "the frame/frames.\n");

		/* Alive notification via Rx interrupt will do the real work */
		if (inta & CSR_INT_BIT_ALIVE)
			IWL_DEBUG_ISR(priv, "Alive interrupt\n");
	}
#endif
	/* Safely ignore these bits for debug checks below */
	inta &= ~(CSR_INT_BIT_SCD | CSR_INT_BIT_ALIVE);

	/* Error detected by uCode */
	if (inta & CSR_INT_BIT_SW_ERR) {
		IWL_ERR(priv, "Microcode SW error detected. "
			"Restarting 0x%X.\n", inta);
		iwl_irq_handle_error(priv);
		handled |= CSR_INT_BIT_SW_ERR;
	}

	/* uCode wakes up after power-down sleep */
	if (inta & CSR_INT_BIT_WAKEUP) {
		IWL_DEBUG_ISR(priv, "Wakeup interrupt\n");
		iwl_rx_queue_update_write_ptr(priv, &priv->rxq);
		iwl_txq_update_write_ptr(priv, &priv->txq[0]);
		iwl_txq_update_write_ptr(priv, &priv->txq[1]);
		iwl_txq_update_write_ptr(priv, &priv->txq[2]);
		iwl_txq_update_write_ptr(priv, &priv->txq[3]);
		iwl_txq_update_write_ptr(priv, &priv->txq[4]);
		iwl_txq_update_write_ptr(priv, &priv->txq[5]);

		handled |= CSR_INT_BIT_WAKEUP;
	}

	/* All uCode command responses, including Tx command responses,
	 * Rx "responses" (frame-received notification), and other
	 * notifications from uCode come through here*/
	if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX)) {
		iwl3945_rx_handle(priv);
		handled |= (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX);
	}

	if (inta & CSR_INT_BIT_FH_TX) {
		IWL_DEBUG_ISR(priv, "Tx interrupt\n");

		iwl_write32(priv, CSR_FH_INT_STATUS, (1 << 6));
		if (!iwl_grab_nic_access(priv)) {
			iwl_write_direct32(priv, FH39_TCSR_CREDIT
					     (FH39_SRVC_CHNL), 0x0);
			iwl_release_nic_access(priv);
		}
		handled |= CSR_INT_BIT_FH_TX;
	}

	if (inta & ~handled)
		IWL_ERR(priv, "Unhandled INTA bits 0x%08x\n", inta & ~handled);

	if (inta & ~CSR_INI_SET_MASK) {
		IWL_WARN(priv, "Disabled INTA bits 0x%08x were pending\n",
			 inta & ~CSR_INI_SET_MASK);
		IWL_WARN(priv, "   with FH_INT = 0x%08x\n", inta_fh);
	}

	/* Re-enable all interrupts */
	/* only Re-enable if disabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl_enable_interrupts(priv);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (priv->debug_level & (IWL_DL_ISR)) {
		inta = iwl_read32(priv, CSR_INT);
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);
		IWL_DEBUG_ISR(priv, "End inta 0x%08x, enabled 0x%08x, fh 0x%08x, "
			"flags 0x%08lx\n", inta, inta_mask, inta_fh, flags);
	}
#endif
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int iwl3945_get_channels_for_scan(struct iwl_priv *priv,
					 enum ieee80211_band band,
				     u8 is_active, u8 n_probes,
				     struct iwl3945_scan_channel *scan_ch)
{
	const struct ieee80211_channel *channels = NULL;
	const struct ieee80211_supported_band *sband;
	const struct iwl_channel_info *ch_info;
	u16 passive_dwell = 0;
	u16 active_dwell = 0;
	int added, i;

	sband = iwl_get_hw_mode(priv, band);
	if (!sband)
		return 0;

	channels = sband->channels;

	active_dwell = iwl_get_active_dwell_time(priv, band, n_probes);
	passive_dwell = iwl_get_passive_dwell_time(priv, band);

	if (passive_dwell <= active_dwell)
		passive_dwell = active_dwell + 1;

	for (i = 0, added = 0; i < sband->n_channels; i++) {
		if (channels[i].flags & IEEE80211_CHAN_DISABLED)
			continue;

		scan_ch->channel = channels[i].hw_value;

		ch_info = iwl_get_channel_info(priv, band, scan_ch->channel);
		if (!is_channel_valid(ch_info)) {
			IWL_DEBUG_SCAN(priv, "Channel %d is INVALID for this band.\n",
				       scan_ch->channel);
			continue;
		}

		scan_ch->active_dwell = cpu_to_le16(active_dwell);
		scan_ch->passive_dwell = cpu_to_le16(passive_dwell);
		/* If passive , set up for auto-switch
		 *  and use long active_dwell time.
		 */
		if (!is_active || is_channel_passive(ch_info) ||
		    (channels[i].flags & IEEE80211_CHAN_PASSIVE_SCAN)) {
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

	IWL_DEBUG_SCAN(priv, "total channels to scan %d \n", added);
	return added;
}

static void iwl3945_init_hw_rates(struct iwl_priv *priv,
			      struct ieee80211_rate *rates)
{
	int i;

	for (i = 0; i < IWL_RATE_COUNT; i++) {
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
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_code);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_data);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_data_backup);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_init);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_init_data);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_boot);
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

	rc = iwl_grab_nic_access(priv);
	if (rc)
		return rc;

	iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR,
			       IWL39_RTC_INST_LOWER_BOUND);

	errcnt = 0;
	for (; len > 0; len -= sizeof(u32), image++) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		val = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
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

	iwl_release_nic_access(priv);

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

	rc = iwl_grab_nic_access(priv);
	if (rc)
		return rc;

	for (i = 0; i < len; i += 100, image += 100/sizeof(u32)) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR,
			i + IWL39_RTC_INST_LOWER_BOUND);
		val = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
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

	iwl_release_nic_access(priv);

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

/**
 * iwl3945_read_ucode - Read uCode images from disk file.
 *
 * Copy into buffers for card to fetch via bus-mastering
 */
static int iwl3945_read_ucode(struct iwl_priv *priv)
{
	struct iwl_ucode *ucode;
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
	if (ucode_raw->size < sizeof(*ucode)) {
		IWL_ERR(priv, "File size way too small!\n");
		ret = -EINVAL;
		goto err_release;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (void *)ucode_raw->data;

	priv->ucode_ver = le32_to_cpu(ucode->ver);
	api_ver = IWL_UCODE_API(priv->ucode_ver);
	inst_size = le32_to_cpu(ucode->inst_size);
	data_size = le32_to_cpu(ucode->data_size);
	init_size = le32_to_cpu(ucode->init_size);
	init_data_size = le32_to_cpu(ucode->init_data_size);
	boot_size = le32_to_cpu(ucode->boot_size);

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
	if (ucode_raw->size < sizeof(*ucode) +
		inst_size + data_size + init_size +
		init_data_size + boot_size) {

		IWL_DEBUG_INFO(priv, "uCode file size %zd too small\n",
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
	src = &ucode->data[0];
	len = priv->ucode_code.len;
	IWL_DEBUG_INFO(priv,
		"Copying (but not loading) uCode instr len %zd\n", len);
	memcpy(priv->ucode_code.v_addr, src, len);
	IWL_DEBUG_INFO(priv, "uCode instr buf vaddr = 0x%p, paddr = 0x%08x\n",
		priv->ucode_code.v_addr, (u32)priv->ucode_code.p_addr);

	/* Runtime data (2nd block)
	 * NOTE:  Copy into backup buffer will be done in iwl3945_up()  */
	src = &ucode->data[inst_size];
	len = priv->ucode_data.len;
	IWL_DEBUG_INFO(priv,
		"Copying (but not loading) uCode data len %zd\n", len);
	memcpy(priv->ucode_data.v_addr, src, len);
	memcpy(priv->ucode_data_backup.v_addr, src, len);

	/* Initialization instructions (3rd block) */
	if (init_size) {
		src = &ucode->data[inst_size + data_size];
		len = priv->ucode_init.len;
		IWL_DEBUG_INFO(priv,
			"Copying (but not loading) init instr len %zd\n", len);
		memcpy(priv->ucode_init.v_addr, src, len);
	}

	/* Initialization data (4th block) */
	if (init_data_size) {
		src = &ucode->data[inst_size + data_size + init_size];
		len = priv->ucode_init_data.len;
		IWL_DEBUG_INFO(priv,
			"Copying (but not loading) init data len %zd\n", len);
		memcpy(priv->ucode_init_data.v_addr, src, len);
	}

	/* Bootstrap instructions (5th block) */
	src = &ucode->data[inst_size + data_size + init_size + init_data_size];
	len = priv->ucode_boot.len;
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
	int rc = 0;
	unsigned long flags;

	/* bits 31:0 for 3945 */
	pinst = priv->ucode_code.p_addr;
	pdata = priv->ucode_data_backup.p_addr;

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

	/* Inst byte count must be last to set up, bit 31 signals uCode
	 *   that all new ptr/size info is in place */
	iwl_write_prph(priv, BSM_DRAM_INST_BYTECOUNT_REG,
				 priv->ucode_code.len | BSM_DRAM_INST_LOAD);

	iwl_release_nic_access(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_INFO(priv, "Runtime uCode pointers are set.\n");

	return rc;
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


/* temporary */
static int iwl3945_mac_beacon_update(struct ieee80211_hw *hw,
				     struct sk_buff *skb);

/**
 * iwl3945_alive_start - called after REPLY_ALIVE notification received
 *                   from protocol/runtime uCode (initialization uCode's
 *                   Alive gets handled by iwl3945_init_alive_start()).
 */
static void iwl3945_alive_start(struct iwl_priv *priv)
{
	int rc = 0;
	int thermal_spin = 0;
	u32 rfkill;

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

	iwl3945_clear_stations_table(priv);

	rc = iwl_grab_nic_access(priv);
	if (rc) {
		IWL_WARN(priv, "Can not read RFKILL status from adapter\n");
		return;
	}

	rfkill = iwl_read_prph(priv, APMG_RFKILL_REG);
	IWL_DEBUG_INFO(priv, "RFKILL status: 0x%x\n", rfkill);
	iwl_release_nic_access(priv);

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

	/* Clear out the uCode error bit if it is set */
	clear_bit(STATUS_FW_ERROR, &priv->status);

	if (iwl_is_rfkill(priv))
		return;

	ieee80211_wake_queues(priv->hw);

	priv->active_rate = priv->rates_mask;
	priv->active_rate_basic = priv->rates_mask & IWL_BASIC_RATES_MASK;

	iwl_power_update_mode(priv, false);

	if (iwl_is_associated(priv)) {
		struct iwl3945_rxon_cmd *active_rxon =
				(struct iwl3945_rxon_cmd *)(&priv->active_rxon);

		memcpy(&priv->staging_rxon, &priv->active_rxon,
		       sizeof(priv->staging_rxon));
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	} else {
		/* Initialize our rx_config data */
		iwl_connection_init_rx_config(priv, priv->iw_mode);
	}

	/* Configure Bluetooth device coexistence support */
	iwl_send_bt_config(priv);

	/* Configure the adapter for unassociated operation */
	iwl3945_commit_rxon(priv);

	iwl3945_reg_txpower_periodic(priv);

	iwl3945_led_register(priv);

	IWL_DEBUG_INFO(priv, "ALIVE processing complete.\n");
	set_bit(STATUS_READY, &priv->status);
	wake_up_interruptible(&priv->wait_command_queue);

	if (priv->error_recovering)
		iwl3945_error_recovery(priv);

	/* reassociate for ADHOC mode */
	if (priv->vif && (priv->iw_mode == NL80211_IFTYPE_ADHOC)) {
		struct sk_buff *beacon = ieee80211_beacon_get(priv->hw,
								priv->vif);
		if (beacon)
			iwl3945_mac_beacon_update(priv->hw, beacon);
	}

	return;

 restart:
	queue_work(priv->workqueue, &priv->restart);
}

static void iwl3945_cancel_deferred_work(struct iwl_priv *priv);

static void __iwl3945_down(struct iwl_priv *priv)
{
	unsigned long flags;
	int exit_pending = test_bit(STATUS_EXIT_PENDING, &priv->status);
	struct ieee80211_conf *conf = NULL;

	IWL_DEBUG_INFO(priv, DRV_NAME " is going down\n");

	conf = ieee80211_get_hw_conf(priv->hw);

	if (!exit_pending)
		set_bit(STATUS_EXIT_PENDING, &priv->status);

	iwl3945_led_unregister(priv);
	iwl3945_clear_stations_table(priv);

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

	/* If we have not previously called iwl3945_init() then
	 * clear all bits but the RF Kill and SUSPEND bits and return */
	if (!iwl_is_init(priv)) {
		priv->status = test_bit(STATUS_RF_KILL_HW, &priv->status) <<
					STATUS_RF_KILL_HW |
			       test_bit(STATUS_RF_KILL_SW, &priv->status) <<
					STATUS_RF_KILL_SW |
			       test_bit(STATUS_GEO_CONFIGURED, &priv->status) <<
					STATUS_GEO_CONFIGURED |
			       test_bit(STATUS_IN_SUSPEND, &priv->status) <<
					STATUS_IN_SUSPEND |
				test_bit(STATUS_EXIT_PENDING, &priv->status) <<
					STATUS_EXIT_PENDING;
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
				STATUS_FW_ERROR |
			test_bit(STATUS_EXIT_PENDING, &priv->status) <<
				STATUS_EXIT_PENDING;

	priv->cfg->ops->lib->apm_ops.reset(priv);
	spin_lock_irqsave(&priv->lock, flags);
	iwl_clear_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl3945_hw_txq_ctx_stop(priv);
	iwl3945_hw_rxq_stop(priv);

	spin_lock_irqsave(&priv->lock, flags);
	if (!iwl_grab_nic_access(priv)) {
		iwl_write_prph(priv, APMG_CLK_DIS_REG,
					 APMG_CLK_VAL_DMA_CLK_RQT);
		iwl_release_nic_access(priv);
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	udelay(5);

	if (exit_pending || test_bit(STATUS_IN_SUSPEND, &priv->status))
		priv->cfg->ops->lib->apm_ops.stop(priv);
	else
		priv->cfg->ops->lib->apm_ops.reset(priv);

 exit:
	memset(&priv->card_alive, 0, sizeof(struct iwl_alive_resp));

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);
	priv->ibss_beacon = NULL;

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

static int __iwl3945_up(struct iwl_priv *priv)
{
	int rc, i;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_WARN(priv, "Exit pending; will not bring the NIC up\n");
		return -EIO;
	}

	if (test_bit(STATUS_RF_KILL_SW, &priv->status)) {
		IWL_WARN(priv, "Radio disabled by SW RF kill (module "
			    "parameter)\n");
		return -ENODEV;
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
		if (!test_bit(STATUS_IN_SUSPEND, &priv->status)) {
			IWL_WARN(priv, "Radio disabled by HW RF Kill switch\n");
			return -ENODEV;
		}
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
	iwl_enable_interrupts(priv);

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

		iwl3945_clear_stations_table(priv);

		/* load bootstrap state machine,
		 * load bootstrap program into processor's memory,
		 * prepare to load the "initialize" uCode */
		priv->cfg->ops->lib->load_ucode(priv);

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

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwl3945_init_alive_start(priv);
	mutex_unlock(&priv->mutex);
}

static void iwl3945_bg_alive_start(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, alive_start.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwl3945_alive_start(priv);
	mutex_unlock(&priv->mutex);
}

static void iwl3945_rfkill_poll(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, rfkill_poll.work);
	unsigned long status = priv->status;

	if (iwl_read32(priv, CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		set_bit(STATUS_RF_KILL_HW, &priv->status);

	if (test_bit(STATUS_RF_KILL_HW, &status) != test_bit(STATUS_RF_KILL_HW, &priv->status))
		queue_work(priv->workqueue, &priv->rf_kill);

	queue_delayed_work(priv->workqueue, &priv->rfkill_poll,
			   round_jiffies_relative(2 * HZ));

}

#define IWL_SCAN_CHECK_WATCHDOG (7 * HZ)
static void iwl3945_bg_request_scan(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, request_scan);
	struct iwl_host_cmd cmd = {
		.id = REPLY_SCAN_CMD,
		.len = sizeof(struct iwl3945_scan_cmd),
		.meta.flags = CMD_SIZE_HUGE,
	};
	int rc = 0;
	struct iwl3945_scan_cmd *scan;
	struct ieee80211_conf *conf = NULL;
	u8 n_probes = 2;
	enum ieee80211_band band;
	DECLARE_SSID_BUF(ssid);

	conf = ieee80211_get_hw_conf(priv->hw);

	mutex_lock(&priv->mutex);

	if (!iwl_is_ready(priv)) {
		IWL_WARN(priv, "request scan called when driver not ready.\n");
		goto done;
	}

	/* Make sure the scan wasn't canceled before this queued work
	 * was given the chance to run... */
	if (!test_bit(STATUS_SCANNING, &priv->status))
		goto done;

	/* This should never be called or scheduled if there is currently
	 * a scan active in the hardware. */
	if (test_bit(STATUS_SCAN_HW, &priv->status)) {
		IWL_DEBUG_INFO(priv, "Multiple concurrent scan requests  "
				"Ignoring second request.\n");
		rc = -EIO;
		goto done;
	}

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Aborting scan due to device shutdown\n");
		goto done;
	}

	if (test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_HC(priv,
			"Scan request while abort pending. Queuing.\n");
		goto done;
	}

	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_HC(priv, "Aborting scan due to RF Kill activation\n");
		goto done;
	}

	if (!test_bit(STATUS_READY, &priv->status)) {
		IWL_DEBUG_HC(priv,
			"Scan request while uninitialized. Queuing.\n");
		goto done;
	}

	if (!priv->scan_bands) {
		IWL_DEBUG_HC(priv, "Aborting scan due to no requested bands\n");
		goto done;
	}

	if (!priv->scan) {
		priv->scan = kmalloc(sizeof(struct iwl3945_scan_cmd) +
				     IWL_MAX_SCAN_SIZE, GFP_KERNEL);
		if (!priv->scan) {
			rc = -ENOMEM;
			goto done;
		}
	}
	scan = priv->scan;
	memset(scan, 0, sizeof(struct iwl3945_scan_cmd) + IWL_MAX_SCAN_SIZE);

	scan->quiet_plcp_th = IWL_PLCP_QUIET_THRESH;
	scan->quiet_time = IWL_ACTIVE_QUIET_TIME;

	if (iwl_is_associated(priv)) {
		u16 interval = 0;
		u32 extra;
		u32 suspend_time = 100;
		u32 scan_suspend_time = 100;
		unsigned long flags;

		IWL_DEBUG_INFO(priv, "Scanning while associated...\n");

		spin_lock_irqsave(&priv->lock, flags);
		interval = priv->beacon_int;
		spin_unlock_irqrestore(&priv->lock, flags);

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

	/* We should add the ability for user to lock to PASSIVE ONLY */
	if (priv->one_direct_scan) {
		IWL_DEBUG_SCAN(priv, "Kicking off one direct scan for '%s'\n",
				print_ssid(ssid, priv->direct_ssid,
				priv->direct_ssid_len));
		scan->direct_scan[0].id = WLAN_EID_SSID;
		scan->direct_scan[0].len = priv->direct_ssid_len;
		memcpy(scan->direct_scan[0].ssid,
		       priv->direct_ssid, priv->direct_ssid_len);
		n_probes++;
	} else
		IWL_DEBUG_SCAN(priv, "Kicking off one indirect scan.\n");

	/* We don't build a direct scan probe request; the uCode will do
	 * that based on the direct_mask added to each channel entry */
	scan->tx_cmd.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK;
	scan->tx_cmd.sta_id = priv->hw_params.bcast_sta_id;
	scan->tx_cmd.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;

	/* flags + rate selection */

	if (priv->scan_bands & BIT(IEEE80211_BAND_2GHZ)) {
		scan->flags = RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK;
		scan->tx_cmd.rate = IWL_RATE_1M_PLCP;
		scan->good_CRC_th = 0;
		band = IEEE80211_BAND_2GHZ;
	} else if (priv->scan_bands & BIT(IEEE80211_BAND_5GHZ)) {
		scan->tx_cmd.rate = IWL_RATE_6M_PLCP;
		scan->good_CRC_th = IWL_GOOD_CRC_TH;
		band = IEEE80211_BAND_5GHZ;
	} else {
		IWL_WARN(priv, "Invalid scan band count\n");
		goto done;
	}

	scan->tx_cmd.len = cpu_to_le16(
		iwl_fill_probe_req(priv, band,
				   (struct ieee80211_mgmt *)scan->data,
				   IWL_MAX_SCAN_SIZE - sizeof(*scan)));

	/* select Rx antennas */
	scan->flags |= iwl3945_get_antenna_flags(priv);

	if (priv->iw_mode == NL80211_IFTYPE_MONITOR)
		scan->filter_flags = RXON_FILTER_PROMISC_MSK;

	scan->channel_count =
		iwl3945_get_channels_for_scan(priv, band, 1, /* active */
					      n_probes,
			(void *)&scan->data[le16_to_cpu(scan->tx_cmd.len)]);

	if (scan->channel_count == 0) {
		IWL_DEBUG_SCAN(priv, "channel count %d\n", scan->channel_count);
		goto done;
	}

	cmd.len += le16_to_cpu(scan->tx_cmd.len) +
	    scan->channel_count * sizeof(struct iwl3945_scan_channel);
	cmd.data = scan;
	scan->len = cpu_to_le16(cmd.len);

	set_bit(STATUS_SCAN_HW, &priv->status);
	rc = iwl_send_cmd_sync(priv, &cmd);
	if (rc)
		goto done;

	queue_delayed_work(priv->workqueue, &priv->scan_check,
			   IWL_SCAN_CHECK_WATCHDOG);

	mutex_unlock(&priv->mutex);
	return;

 done:
	/* can not perform scan make sure we clear scanning
	 * bits from status so next scan request can be performed.
	 * if we dont clear scanning status bit here all next scan
	 * will fail
	*/
	clear_bit(STATUS_SCAN_HW, &priv->status);
	clear_bit(STATUS_SCANNING, &priv->status);

	/* inform mac80211 scan aborted */
	queue_work(priv->workqueue, &priv->scan_completed);
	mutex_unlock(&priv->mutex);
}

static void iwl3945_bg_up(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, up);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	__iwl3945_up(priv);
	mutex_unlock(&priv->mutex);
	iwl_rfkill_set_hw_state(priv);
}

static void iwl3945_bg_restart(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, restart);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	iwl3945_down(priv);
	queue_work(priv->workqueue, &priv->up);
}

static void iwl3945_bg_rx_replenish(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, rx_replenish);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwl3945_rx_replenish(priv);
	mutex_unlock(&priv->mutex);
}

#define IWL_DELAY_NEXT_SCAN (HZ*2)

static void iwl3945_post_associate(struct iwl_priv *priv)
{
	int rc = 0;
	struct ieee80211_conf *conf = NULL;

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
	iwl3945_commit_rxon(priv);

	memset(&priv->rxon_timing, 0, sizeof(struct iwl_rxon_time_cmd));
	iwl3945_setup_rxon_timing(priv);
	rc = iwl_send_cmd_pdu(priv, REPLY_RXON_TIMING,
			      sizeof(priv->rxon_timing), &priv->rxon_timing);
	if (rc)
		IWL_WARN(priv, "REPLY_RXON_TIMING failed - "
			    "Attempting to continue.\n");

	priv->staging_rxon.filter_flags |= RXON_FILTER_ASSOC_MSK;

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

	iwl3945_commit_rxon(priv);

	switch (priv->iw_mode) {
	case NL80211_IFTYPE_STATION:
		iwl3945_rate_scale_init(priv->hw, IWL_AP_ID);
		break;

	case NL80211_IFTYPE_ADHOC:

		priv->assoc_id = 1;
		iwl3945_add_station(priv, priv->bssid, 0, 0);
		iwl3945_sync_sta(priv, IWL_STA_ID,
				 (priv->band == IEEE80211_BAND_5GHZ) ?
				 IWL_RATE_6M_PLCP : IWL_RATE_1M_PLCP,
				 CMD_ASYNC);
		iwl3945_rate_scale_init(priv->hw, IWL_STA_ID);
		iwl3945_send_beacon_cmd(priv);

		break;

	default:
		 IWL_ERR(priv, "%s Should not be called in %d mode\n",
			   __func__, priv->iw_mode);
		break;
	}

	iwl_activate_qos(priv, 0);

	/* we have just associated, don't start scan too early */
	priv->next_scan_jiffies = jiffies + IWL_DELAY_NEXT_SCAN;
}

static int iwl3945_mac_config(struct ieee80211_hw *hw, u32 changed);

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

	memset(&priv->staging_rxon, 0, sizeof(priv->staging_rxon));
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

	iwl_rfkill_set_hw_state(priv);

	if (ret)
		goto out_release_irq;

	IWL_DEBUG_INFO(priv, "Start UP work.\n");

	if (test_bit(STATUS_IN_SUSPEND, &priv->status))
		return 0;

	/* Wait for START_ALIVE from ucode. Otherwise callbacks from
	 * mac80211 will not be run successfully. */
	ret = wait_event_interruptible_timeout(priv->wait_command_queue,
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
	cancel_delayed_work(&priv->rfkill_poll);

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

	if (iwl_is_ready_rf(priv)) {
		/* stop mac, cancel any scan request and clear
		 * RXON_FILTER_ASSOC_MSK BIT
		 */
		mutex_lock(&priv->mutex);
		iwl_scan_cancel_timeout(priv, 100);
		mutex_unlock(&priv->mutex);
	}

	iwl3945_down(priv);

	flush_workqueue(priv->workqueue);

	/* start polling the killswitch state again */
	queue_delayed_work(priv->workqueue, &priv->rfkill_poll,
			   round_jiffies_relative(2 * HZ));

	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static int iwl3945_mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	IWL_DEBUG_TX(priv, "dev->xmit(%d bytes) at rate 0x%02x\n", skb->len,
		     ieee80211_get_tx_rate(hw, IEEE80211_SKB_CB(skb))->bitrate);

	if (iwl3945_tx_skb(priv, skb))
		dev_kfree_skb_any(skb);

	IWL_DEBUG_MAC80211(priv, "leave\n");
	return NETDEV_TX_OK;
}

static int iwl3945_mac_add_interface(struct ieee80211_hw *hw,
				 struct ieee80211_if_init_conf *conf)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;

	IWL_DEBUG_MAC80211(priv, "enter: type %d\n", conf->type);

	if (priv->vif) {
		IWL_DEBUG_MAC80211(priv, "leave - vif != NULL\n");
		return -EOPNOTSUPP;
	}

	spin_lock_irqsave(&priv->lock, flags);
	priv->vif = conf->vif;
	priv->iw_mode = conf->type;

	spin_unlock_irqrestore(&priv->lock, flags);

	mutex_lock(&priv->mutex);

	if (conf->mac_addr) {
		IWL_DEBUG_MAC80211(priv, "Set: %pM\n", conf->mac_addr);
		memcpy(priv->mac_addr, conf->mac_addr, ETH_ALEN);
	}

	if (iwl_is_ready(priv))
		iwl3945_set_mode(priv, conf->type);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");
	return 0;
}

/**
 * iwl3945_mac_config - mac80211 config callback
 *
 * We ignore conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME since it seems to
 * be set inappropriately and the driver currently sets the hardware up to
 * use it whenever needed.
 */
static int iwl3945_mac_config(struct ieee80211_hw *hw, u32 changed)
{
	struct iwl_priv *priv = hw->priv;
	const struct iwl_channel_info *ch_info;
	struct ieee80211_conf *conf = &hw->conf;
	unsigned long flags;
	int ret = 0;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211(priv, "enter to channel %d\n",
				conf->channel->hw_value);

	if (!iwl_is_ready(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - not ready\n");
		ret = -EIO;
		goto out;
	}

	if (unlikely(!iwl3945_mod_params.disable_hw_scan &&
		     test_bit(STATUS_SCANNING, &priv->status))) {
		IWL_DEBUG_MAC80211(priv, "leave - scanning\n");
		set_bit(STATUS_CONF_PENDING, &priv->status);
		mutex_unlock(&priv->mutex);
		return 0;
	}

	spin_lock_irqsave(&priv->lock, flags);

	ch_info = iwl_get_channel_info(priv, conf->channel->band,
				       conf->channel->hw_value);
	if (!is_channel_valid(ch_info)) {
		IWL_DEBUG_SCAN(priv,
				"Channel %d [%d] is INVALID for this band.\n",
				conf->channel->hw_value, conf->channel->band);
		IWL_DEBUG_MAC80211(priv, "leave - invalid channel\n");
		spin_unlock_irqrestore(&priv->lock, flags);
		ret = -EINVAL;
		goto out;
	}

	iwl_set_rxon_channel(priv, conf->channel);

	iwl_set_flags_for_band(priv, conf->channel->band);

	/* The list of supported rates and rate mask can be different
	 * for each phymode; since the phymode may have changed, reset
	 * the rate mask to what mac80211 lists */
	iwl_set_rate(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

#ifdef IEEE80211_CONF_CHANNEL_SWITCH
	if (conf->flags & IEEE80211_CONF_CHANNEL_SWITCH) {
		iwl3945_hw_channel_switch(priv, conf->channel);
		goto out;
	}
#endif

	if (changed & IEEE80211_CONF_CHANGE_RADIO_ENABLED) {
		if (conf->radio_enabled &&
		    iwl_radio_kill_sw_enable_radio(priv)) {
			IWL_DEBUG_MAC80211(priv, "leave - RF-KILL - "
						 "waiting for uCode\n");
			goto out;
		}

		if (!conf->radio_enabled) {
			iwl_radio_kill_sw_disable_radio(priv);
			IWL_DEBUG_MAC80211(priv, "leave - radio disabled\n");
			goto out;
		}
	}

	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - RF kill\n");
		ret = -EIO;
		goto out;
	}

	iwl_set_rate(priv);

	if (memcmp(&priv->active_rxon,
		   &priv->staging_rxon, sizeof(priv->staging_rxon)))
		iwl3945_commit_rxon(priv);
	else
		IWL_DEBUG_INFO(priv, "Not re-sending same RXON configuration\n");

	IWL_DEBUG_MAC80211(priv, "leave\n");

out:
	clear_bit(STATUS_CONF_PENDING, &priv->status);
	mutex_unlock(&priv->mutex);
	return ret;
}

static void iwl3945_config_ap(struct iwl_priv *priv)
{
	int rc = 0;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* The following should be done only at AP bring up */
	if (!(iwl_is_associated(priv))) {

		/* RXON - unassoc (to set timing command) */
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl3945_commit_rxon(priv);

		/* RXON Timing */
		memset(&priv->rxon_timing, 0, sizeof(struct iwl_rxon_time_cmd));
		iwl3945_setup_rxon_timing(priv);
		rc = iwl_send_cmd_pdu(priv, REPLY_RXON_TIMING,
				      sizeof(priv->rxon_timing),
				      &priv->rxon_timing);
		if (rc)
			IWL_WARN(priv, "REPLY_RXON_TIMING failed - "
					"Attempting to continue.\n");

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
		iwl3945_commit_rxon(priv);
		iwl3945_add_station(priv, iwl_bcast_addr, 0, 0);
	}
	iwl3945_send_beacon_cmd(priv);

	/* FIXME - we need to add code here to detect a totally new
	 * configuration, reset the AP, unassoc, rxon timing, assoc,
	 * clear sta table, add BCAST sta... */
}

static int iwl3945_mac_config_interface(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_if_conf *conf)
{
	struct iwl_priv *priv = hw->priv;
	int rc;

	if (conf == NULL)
		return -EIO;

	if (priv->vif != vif) {
		IWL_DEBUG_MAC80211(priv, "leave - priv->vif != vif\n");
		return 0;
	}

	/* handle this temporarily here */
	if (priv->iw_mode == NL80211_IFTYPE_ADHOC &&
	    conf->changed & IEEE80211_IFCC_BEACON) {
		struct sk_buff *beacon = ieee80211_beacon_get(hw, vif);
		if (!beacon)
			return -ENOMEM;
		mutex_lock(&priv->mutex);
		rc = iwl3945_mac_beacon_update(hw, beacon);
		mutex_unlock(&priv->mutex);
		if (rc)
			return rc;
	}

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	mutex_lock(&priv->mutex);

	if (conf->bssid)
		IWL_DEBUG_MAC80211(priv, "bssid: %pM\n", conf->bssid);

/*
 * very dubious code was here; the probe filtering flag is never set:
 *
	if (unlikely(test_bit(STATUS_SCANNING, &priv->status)) &&
	    !(priv->hw->flags & IEEE80211_HW_NO_PROBE_FILTERING)) {
 */

	if (priv->iw_mode == NL80211_IFTYPE_AP) {
		if (!conf->bssid) {
			conf->bssid = priv->mac_addr;
			memcpy(priv->bssid, priv->mac_addr, ETH_ALEN);
			IWL_DEBUG_MAC80211(priv, "bssid was set to: %pM\n",
					   conf->bssid);
		}
		if (priv->ibss_beacon)
			dev_kfree_skb(priv->ibss_beacon);

		priv->ibss_beacon = ieee80211_beacon_get(hw, vif);
	}

	if (iwl_is_rfkill(priv))
		goto done;

	if (conf->bssid && !is_zero_ether_addr(conf->bssid) &&
	    !is_multicast_ether_addr(conf->bssid)) {
		/* If there is currently a HW scan going on in the background
		 * then we need to cancel it else the RXON below will fail. */
		if (iwl_scan_cancel_timeout(priv, 100)) {
			IWL_WARN(priv, "Aborted scan still in progress "
				    "after 100ms\n");
			IWL_DEBUG_MAC80211(priv, "leaving:scan abort failed\n");
			mutex_unlock(&priv->mutex);
			return -EAGAIN;
		}
		memcpy(priv->staging_rxon.bssid_addr, conf->bssid, ETH_ALEN);

		/* TODO: Audit driver for usage of these members and see
		 * if mac80211 deprecates them (priv->bssid looks like it
		 * shouldn't be there, but I haven't scanned the IBSS code
		 * to verify) - jpk */
		memcpy(priv->bssid, conf->bssid, ETH_ALEN);

		if (priv->iw_mode == NL80211_IFTYPE_AP)
			iwl3945_config_ap(priv);
		else {
			rc = iwl3945_commit_rxon(priv);
			if ((priv->iw_mode == NL80211_IFTYPE_STATION) && rc)
				iwl3945_add_station(priv,
					priv->active_rxon.bssid_addr, 1, 0);
		}

	} else {
		iwl_scan_cancel_timeout(priv, 100);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl3945_commit_rxon(priv);
	}

 done:
	IWL_DEBUG_MAC80211(priv, "leave\n");
	mutex_unlock(&priv->mutex);

	return 0;
}

static void iwl3945_mac_remove_interface(struct ieee80211_hw *hw,
				     struct ieee80211_if_init_conf *conf)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	mutex_lock(&priv->mutex);

	if (iwl_is_ready_rf(priv)) {
		iwl_scan_cancel_timeout(priv, 100);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl3945_commit_rxon(priv);
	}
	if (priv->vif == conf->vif) {
		priv->vif = NULL;
		memset(priv->bssid, 0, ETH_ALEN);
	}
	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}

#define IWL_DELAY_NEXT_SCAN_AFTER_ASSOC (HZ*6)

static void iwl3945_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u32 changes)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211(priv, "changes = 0x%X\n", changes);

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		IWL_DEBUG_MAC80211(priv, "ERP_PREAMBLE %d\n",
				   bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			priv->staging_rxon.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			priv->staging_rxon.flags &=
				~RXON_FLG_SHORT_PREAMBLE_MSK;
	}

	if (changes & BSS_CHANGED_ERP_CTS_PROT) {
		IWL_DEBUG_MAC80211(priv, "ERP_CTS %d\n",
				   bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot && (priv->band != IEEE80211_BAND_5GHZ))
			priv->staging_rxon.flags |= RXON_FLG_TGG_PROTECT_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_TGG_PROTECT_MSK;
	}

	if (changes & BSS_CHANGED_ASSOC) {
		IWL_DEBUG_MAC80211(priv, "ASSOC %d\n", bss_conf->assoc);
		/* This should never happen as this function should
		 * never be called from interrupt context. */
		if (WARN_ON_ONCE(in_interrupt()))
			return;
		if (bss_conf->assoc) {
			priv->assoc_id = bss_conf->aid;
			priv->beacon_int = bss_conf->beacon_int;
			priv->timestamp = bss_conf->timestamp;
			priv->assoc_capability = bss_conf->assoc_capability;
			priv->power_data.dtim_period = bss_conf->dtim_period;
			priv->next_scan_jiffies = jiffies +
					IWL_DELAY_NEXT_SCAN_AFTER_ASSOC;
			mutex_lock(&priv->mutex);
			iwl3945_post_associate(priv);
			mutex_unlock(&priv->mutex);
		} else {
			priv->assoc_id = 0;
			IWL_DEBUG_MAC80211(priv,
					"DISASSOC %d\n", bss_conf->assoc);
		}
	} else if (changes && iwl_is_associated(priv) && priv->assoc_id) {
			IWL_DEBUG_MAC80211(priv,
					"Associated Changes %d\n", changes);
			iwl3945_send_rxon_assoc(priv);
	}

}

static int iwl3945_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct ieee80211_key_conf *key)
{
	struct iwl_priv *priv = hw->priv;
	const u8 *addr;
	int ret = 0;
	u8 sta_id = IWL_INVALID_STATION;
	u8 static_key;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (iwl3945_mod_params.sw_crypto) {
		IWL_DEBUG_MAC80211(priv, "leave - hwcrypto disabled\n");
		return -EOPNOTSUPP;
	}

	addr = sta ? sta->addr : iwl_bcast_addr;
	static_key = !iwl_is_associated(priv);

	if (!static_key) {
		sta_id = iwl3945_hw_find_station(priv, addr);
		if (sta_id == IWL_INVALID_STATION) {
			IWL_DEBUG_MAC80211(priv, "leave - %pM not in station map.\n",
					    addr);
			return -EINVAL;
		}
	}

	mutex_lock(&priv->mutex);
	iwl_scan_cancel_timeout(priv, 100);
	mutex_unlock(&priv->mutex);

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

	IWL_DEBUG_MAC80211(priv, "leave\n");

	return ret;
}

static int iwl3945_mac_conf_tx(struct ieee80211_hw *hw, u16 queue,
			   const struct ieee80211_tx_queue_params *params)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	int q;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - RF not ready\n");
		return -EIO;
	}

	if (queue >= AC_NUM) {
		IWL_DEBUG_MAC80211(priv, "leave - queue >= AC_NUM %d\n", queue);
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
	if (priv->iw_mode == NL80211_IFTYPE_AP)
		iwl_activate_qos(priv, 1);
	else if (priv->assoc_id && iwl_is_associated(priv))
		iwl_activate_qos(priv, 0);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");
	return 0;
}

static int iwl3945_mac_get_tx_stats(struct ieee80211_hw *hw,
				struct ieee80211_tx_queue_stats *stats)
{
	struct iwl_priv *priv = hw->priv;
	int i, avail;
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;
	unsigned long flags;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - RF not ready\n");
		return -EIO;
	}

	spin_lock_irqsave(&priv->lock, flags);

	for (i = 0; i < AC_NUM; i++) {
		txq = &priv->txq[i];
		q = &txq->q;
		avail = iwl_queue_space(q);

		stats[i].len = q->n_window - avail;
		stats[i].limit = q->n_window - q->high_mark;
		stats[i].count = q->n_window;

	}
	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_MAC80211(priv, "leave\n");

	return 0;
}

static void iwl3945_mac_reset_tsf(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211(priv, "enter\n");

	iwl_reset_qos(priv);

	spin_lock_irqsave(&priv->lock, flags);
	priv->assoc_id = 0;
	priv->assoc_capability = 0;

	/* new association get rid of ibss beacon skb */
	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = NULL;

	priv->beacon_int = priv->hw->conf.beacon_int;
	priv->timestamp = 0;
	if ((priv->iw_mode == NL80211_IFTYPE_STATION))
		priv->beacon_int = 0;

	spin_unlock_irqrestore(&priv->lock, flags);

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - not ready\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	/* we are restarting association process
	 * clear RXON_FILTER_ASSOC_MSK bit
	*/
	if (priv->iw_mode != NL80211_IFTYPE_AP) {
		iwl_scan_cancel_timeout(priv, 100);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl3945_commit_rxon(priv);
	}

	/* Per mac80211.h: This is only used in IBSS mode... */
	if (priv->iw_mode != NL80211_IFTYPE_ADHOC) {

		IWL_DEBUG_MAC80211(priv, "leave - not in IBSS\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	iwl_set_rate(priv);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");

}

static int iwl3945_mac_beacon_update(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	__le64 timestamp;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - RF not ready\n");
		return -EIO;
	}

	if (priv->iw_mode != NL80211_IFTYPE_ADHOC) {
		IWL_DEBUG_MAC80211(priv, "leave - not IBSS\n");
		return -EIO;
	}

	spin_lock_irqsave(&priv->lock, flags);

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = skb;

	priv->assoc_id = 0;
	timestamp = ((struct ieee80211_mgmt *)skb->data)->u.beacon.timestamp;
	priv->timestamp = le64_to_cpu(timestamp);

	IWL_DEBUG_MAC80211(priv, "leave\n");
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_reset_qos(priv);

	iwl3945_post_associate(priv);


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
static ssize_t show_debug_level(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = d->driver_data;

	return sprintf(buf, "0x%08X\n", priv->debug_level);
}
static ssize_t store_debug_level(struct device *d,
				struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct iwl_priv *priv = d->driver_data;
	unsigned long val;
	int ret;

	ret = strict_strtoul(buf, 0, &val);
	if (ret)
		IWL_INFO(priv, "%s is not in hex or decimal form.\n", buf);
	else
		priv->debug_level = val;

	return strnlen(buf, count);
}

static DEVICE_ATTR(debug_level, S_IWUSR | S_IRUGO,
			show_debug_level, store_debug_level);

#endif /* CONFIG_IWLWIFI_DEBUG */

static ssize_t show_temperature(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	return sprintf(buf, "%d\n", iwl3945_hw_get_temperature(priv));
}

static DEVICE_ATTR(temperature, S_IRUGO, show_temperature, NULL);

static ssize_t show_tx_power(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	return sprintf(buf, "%d\n", priv->tx_power_user_lmt);
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
		IWL_INFO(priv, ": %s is not in decimal form.\n", buf);
	else
		iwl3945_hw_reg_set_txpower(priv, val);

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
		if (iwl_scan_cancel_timeout(priv, 100))
			IWL_WARN(priv, "Could not cancel scan.\n");
		else {
			IWL_DEBUG_INFO(priv, "Committing rxon.flags = 0x%04X\n",
				       flags);
			priv->staging_rxon.flags = cpu_to_le32(flags);
			iwl3945_commit_rxon(priv);
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
		if (iwl_scan_cancel_timeout(priv, 100))
			IWL_WARN(priv, "Could not cancel scan.\n");
		else {
			IWL_DEBUG_INFO(priv, "Committing rxon.filter_flags = "
				       "0x%04X\n", filter_flags);
			priv->staging_rxon.filter_flags =
				cpu_to_le32(filter_flags);
			iwl3945_commit_rxon(priv);
		}
	}
	mutex_unlock(&priv->mutex);

	return count;
}

static DEVICE_ATTR(filter_flags, S_IWUSR | S_IRUGO, show_filter_flags,
		   store_filter_flags);

#ifdef CONFIG_IWL3945_SPECTRUM_MEASUREMENT

static ssize_t show_measurement(struct device *d,
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

	IWL_DEBUG_INFO(priv, "Invoking measurement of type %d on "
		       "channel %d (for '%s')\n", type, params.channel, buf);
	iwl3945_get_measurement(priv, &params, type);

	return count;
}

static DEVICE_ATTR(measurement, S_IRUSR | S_IWUSR,
		   show_measurement, store_measurement);
#endif /* CONFIG_IWL3945_SPECTRUM_MEASUREMENT */

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
	int ret;
	unsigned long mode;


	mutex_lock(&priv->mutex);

	ret = strict_strtoul(buf, 10, &mode);
	if (ret)
		goto out;

	ret = iwl_power_set_user_mode(priv, mode);
	if (ret) {
		IWL_DEBUG_MAC80211(priv, "failed setting power mode.\n");
		goto out;
	}
	ret = count;

 out:
	mutex_unlock(&priv->mutex);
	return ret;
}

static ssize_t show_power_level(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	int mode = priv->power_data.user_power_setting;
	int system = priv->power_data.system_power_setting;
	int level = priv->power_data.power_mode;
	char *p = buf;

	switch (system) {
	case IWL_POWER_SYS_AUTO:
		p += sprintf(p, "SYSTEM:auto");
		break;
	case IWL_POWER_SYS_AC:
		p += sprintf(p, "SYSTEM:ac");
		break;
	case IWL_POWER_SYS_BATTERY:
		p += sprintf(p, "SYSTEM:battery");
		break;
	}

	p += sprintf(p, "\tMODE:%s", (mode < IWL_POWER_AUTO) ?
			"fixed" : "auto");
	p += sprintf(p, "\tINDEX:%d", level);
	p += sprintf(p, "\n");
	return p - buf + 1;
}

static DEVICE_ATTR(power_level, S_IWUSR | S_IRUSR,
		   show_power_level, store_power_level);

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
	u32 size = sizeof(struct iwl3945_notif_statistics);
	u32 len = 0, ofs = 0;
	u8 *data = (u8 *)&priv->statistics_39;
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

	return sprintf(buf, "%d\n", iwl3945_mod_params.antenna);
}

static ssize_t store_antenna(struct device *d,
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
		iwl3945_dump_nic_error_log((struct iwl_priv *)d->driver_data);

	return strnlen(buf, count);
}

static DEVICE_ATTR(dump_errors, S_IWUSR, NULL, dump_error_log);

static ssize_t dump_event_log(struct device *d,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	char *p = (char *)buf;

	if (p[0] == '1')
		iwl3945_dump_nic_event_log((struct iwl_priv *)d->driver_data);

	return strnlen(buf, count);
}

static DEVICE_ATTR(dump_events, S_IWUSR, NULL, dump_event_log);

/*****************************************************************************
 *
 * driver setup and tear down
 *
 *****************************************************************************/

static void iwl3945_setup_deferred_work(struct iwl_priv *priv)
{
	priv->workqueue = create_singlethread_workqueue(DRV_NAME);

	init_waitqueue_head(&priv->wait_command_queue);

	INIT_WORK(&priv->up, iwl3945_bg_up);
	INIT_WORK(&priv->restart, iwl3945_bg_restart);
	INIT_WORK(&priv->rx_replenish, iwl3945_bg_rx_replenish);
	INIT_WORK(&priv->rf_kill, iwl_bg_rf_kill);
	INIT_WORK(&priv->beacon_update, iwl3945_bg_beacon_update);
	INIT_DELAYED_WORK(&priv->init_alive_start, iwl3945_bg_init_alive_start);
	INIT_DELAYED_WORK(&priv->alive_start, iwl3945_bg_alive_start);
	INIT_DELAYED_WORK(&priv->rfkill_poll, iwl3945_rfkill_poll);
	INIT_WORK(&priv->scan_completed, iwl_bg_scan_completed);
	INIT_WORK(&priv->request_scan, iwl3945_bg_request_scan);
	INIT_WORK(&priv->abort_scan, iwl_bg_abort_scan);
	INIT_DELAYED_WORK(&priv->scan_check, iwl_bg_scan_check);

	iwl3945_hw_setup_deferred_work(priv);

	tasklet_init(&priv->irq_tasklet, (void (*)(unsigned long))
		     iwl3945_irq_tasklet, (unsigned long)priv);
}

static void iwl3945_cancel_deferred_work(struct iwl_priv *priv)
{
	iwl3945_hw_cancel_deferred_work(priv);

	cancel_delayed_work_sync(&priv->init_alive_start);
	cancel_delayed_work(&priv->scan_check);
	cancel_delayed_work(&priv->alive_start);
	cancel_work_sync(&priv->beacon_update);
}

static struct attribute *iwl3945_sysfs_entries[] = {
	&dev_attr_antenna.attr,
	&dev_attr_channels.attr,
	&dev_attr_dump_errors.attr,
	&dev_attr_dump_events.attr,
	&dev_attr_flags.attr,
	&dev_attr_filter_flags.attr,
#ifdef CONFIG_IWL3945_SPECTRUM_MEASUREMENT
	&dev_attr_measurement.attr,
#endif
	&dev_attr_power_level.attr,
	&dev_attr_retry_rate.attr,
	&dev_attr_statistics.attr,
	&dev_attr_status.attr,
	&dev_attr_temperature.attr,
	&dev_attr_tx_power.attr,
#ifdef CONFIG_IWLWIFI_DEBUG
	&dev_attr_debug_level.attr,
#endif
	NULL
};

static struct attribute_group iwl3945_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = iwl3945_sysfs_entries,
};

static struct ieee80211_ops iwl3945_hw_ops = {
	.tx = iwl3945_mac_tx,
	.start = iwl3945_mac_start,
	.stop = iwl3945_mac_stop,
	.add_interface = iwl3945_mac_add_interface,
	.remove_interface = iwl3945_mac_remove_interface,
	.config = iwl3945_mac_config,
	.config_interface = iwl3945_mac_config_interface,
	.configure_filter = iwl_configure_filter,
	.set_key = iwl3945_mac_set_key,
	.get_tx_stats = iwl3945_mac_get_tx_stats,
	.conf_tx = iwl3945_mac_conf_tx,
	.reset_tsf = iwl3945_mac_reset_tsf,
	.bss_info_changed = iwl3945_bss_info_changed,
	.hw_scan = iwl_mac_hw_scan
};

static int iwl3945_init_drv(struct iwl_priv *priv)
{
	int ret;
	struct iwl3945_eeprom *eeprom = (struct iwl3945_eeprom *)priv->eeprom;

	priv->retry_rate = 1;
	priv->ibss_beacon = NULL;

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->power_data.lock);
	spin_lock_init(&priv->sta_lock);
	spin_lock_init(&priv->hcmd_lock);

	INIT_LIST_HEAD(&priv->free_frames);

	mutex_init(&priv->mutex);

	/* Clear the driver's (not device's) station table */
	iwl3945_clear_stations_table(priv);

	priv->data_retry_limit = -1;
	priv->ieee_channels = NULL;
	priv->ieee_rates = NULL;
	priv->band = IEEE80211_BAND_2GHZ;

	priv->iw_mode = NL80211_IFTYPE_STATION;

	iwl_reset_qos(priv);

	priv->qos_data.qos_active = 0;
	priv->qos_data.qos_cap.val = 0;

	priv->rates_mask = IWL_RATES_MASK;
	/* If power management is turned on, default to CAM mode */
	priv->power_mode = IWL_POWER_MODE_CAM;
	priv->tx_power_user_lmt = IWL_DEFAULT_TX_POWER;

	if (eeprom->version < EEPROM_3945_EEPROM_VERSION) {
		IWL_WARN(priv, "Unsupported EEPROM version: 0x%04X\n",
			 eeprom->version);
		ret = -EINVAL;
		goto err;
	}
	ret = iwl_init_channel_map(priv);
	if (ret) {
		IWL_ERR(priv, "initializing regulatory failed: %d\n", ret);
		goto err;
	}

	/* Set up txpower settings in driver for all channels */
	if (iwl3945_txpower_set_from_eeprom(priv)) {
		ret = -EIO;
		goto err_free_channel_map;
	}

	ret = iwlcore_init_geos(priv);
	if (ret) {
		IWL_ERR(priv, "initializing geos failed: %d\n", ret);
		goto err_free_channel_map;
	}
	iwl3945_init_hw_rates(priv, priv->ieee_rates);

	return 0;

err_free_channel_map:
	iwl_free_channel_map(priv);
err:
	return ret;
}

static int iwl3945_setup_mac(struct iwl_priv *priv)
{
	int ret;
	struct ieee80211_hw *hw = priv->hw;

	hw->rate_control_algorithm = "iwl-3945-rs";
	hw->sta_data_size = sizeof(struct iwl3945_sta_priv);

	/* Tell mac80211 our characteristics */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_NOISE_DBM |
		    IEEE80211_HW_SPECTRUM_MGMT;

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->custom_regulatory = true;

	hw->wiphy->max_scan_ssids = 1; /* WILL FIX */

	/* Default value; 4 EDCA QOS priorities */
	hw->queues = 4;

	hw->conf.beacon_int = 100;

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

static int iwl3945_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = 0;
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
	hw = iwl_alloc_all(cfg, &iwl3945_hw_ops);
	if (hw == NULL) {
		printk(KERN_ERR DRV_NAME "Can not allocate network device\n");
		err = -ENOMEM;
		goto out;
	}
	priv = hw->priv;
	SET_IEEE80211_DEV(hw, &pdev->dev);

	if ((iwl3945_mod_params.num_of_queues > IWL39_MAX_NUM_QUEUES) ||
	     (iwl3945_mod_params.num_of_queues < IWL_MIN_NUM_QUEUES)) {
		IWL_ERR(priv,
			"invalid queues_num, should be between %d and %d\n",
			IWL_MIN_NUM_QUEUES, IWL39_MAX_NUM_QUEUES);
		err = -EINVAL;
		goto out_ieee80211_free_hw;
	}

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

#ifdef CONFIG_IWLWIFI_DEBUG
	priv->debug_level = iwl3945_mod_params.debug;
	atomic_set(&priv->restrict_refcnt, 0);
#endif

	/***************************
	 * 2. Initializing PCI bus
	 * *************************/
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

	/* amp init */
	err = priv->cfg->ops->lib->apm_ops.init(priv);
	if (err < 0) {
		IWL_DEBUG_INFO(priv, "Failed to init the card\n");
		goto out_iounmap;
	}

	/***********************
	 * 4. Read EEPROM
	 * ********************/

	/* Read the EEPROM */
	err = iwl_eeprom_init(priv);
	if (err) {
		IWL_ERR(priv, "Unable to init EEPROM\n");
		goto out_iounmap;
	}
	/* MAC Address location in EEPROM same for 3945/4965 */
	eeprom = (struct iwl3945_eeprom *)priv->eeprom;
	memcpy(priv->mac_addr, eeprom->mac_address, ETH_ALEN);
	IWL_DEBUG_INFO(priv, "MAC address: %pM\n", priv->mac_addr);
	SET_IEEE80211_PERM_ADDR(priv->hw, priv->mac_addr);

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

	/***********************************
	 * 7. Initialize Module Parameters
	 * **********************************/

	/* Initialize module parameter values here */
	/* Disable radio (SW RF KILL) via parameter when loading driver */
	if (iwl3945_mod_params.disable) {
		set_bit(STATUS_RF_KILL_SW, &priv->status);
		IWL_DEBUG_INFO(priv, "Radio disabled.\n");
	}


	/***********************
	 * 8. Setup Services
	 * ********************/

	spin_lock_irqsave(&priv->lock, flags);
	iwl_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	pci_enable_msi(priv->pci_dev);

	err = request_irq(priv->pci_dev->irq, iwl_isr, IRQF_SHARED,
			  DRV_NAME, priv);
	if (err) {
		IWL_ERR(priv, "Error allocating IRQ %d\n", priv->pci_dev->irq);
		goto out_disable_msi;
	}

	err = sysfs_create_group(&pdev->dev.kobj, &iwl3945_attribute_group);
	if (err) {
		IWL_ERR(priv, "failed to create sysfs device attributes\n");
		goto out_release_irq;
	}

	iwl_set_rxon_channel(priv,
			     &priv->bands[IEEE80211_BAND_2GHZ].channels[5]);
	iwl3945_setup_deferred_work(priv);
	iwl3945_setup_rx_handlers(priv);

	/*********************************
	 * 9. Setup and Register mac80211
	 * *******************************/

	iwl_enable_interrupts(priv);

	err = iwl3945_setup_mac(priv);
	if (err)
		goto  out_remove_sysfs;

	err = iwl_rfkill_init(priv);
	if (err)
		IWL_ERR(priv, "Unable to initialize RFKILL system. "
				  "Ignoring error: %d\n", err);
	else
		iwl_rfkill_set_hw_state(priv);

	/* Start monitoring the killswitch */
	queue_delayed_work(priv->workqueue, &priv->rfkill_poll,
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
	iwlcore_free_geos(priv);
	iwl_free_channel_map(priv);
 out_unset_hw_params:
	iwl3945_unset_hw_params(priv);
 out_eeprom_free:
	iwl_eeprom_free(priv);
 out_iounmap:
	pci_iounmap(pdev, priv->hw_base);
 out_pci_release_regions:
	pci_release_regions(pdev);
 out_pci_disable_device:
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
 out_ieee80211_free_hw:
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

	set_bit(STATUS_EXIT_PENDING, &priv->status);

	if (priv->mac80211_registered) {
		ieee80211_unregister_hw(priv->hw);
		priv->mac80211_registered = 0;
	} else {
		iwl3945_down(priv);
	}

	/* make sure we flush any pending irq or
	 * tasklet for the driver
	 */
	spin_lock_irqsave(&priv->lock, flags);
	iwl_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_synchronize_irq(priv);

	sysfs_remove_group(&pdev->dev.kobj, &iwl3945_attribute_group);

	iwl_rfkill_unregister(priv);
	cancel_delayed_work_sync(&priv->rfkill_poll);

	iwl3945_dealloc_ucode_pci(priv);

	if (priv->rxq.bd)
		iwl3945_rx_queue_free(priv, &priv->rxq);
	iwl3945_hw_txq_ctx_free(priv);

	iwl3945_unset_hw_params(priv);
	iwl3945_clear_stations_table(priv);

	/*netif_stop_queue(dev); */
	flush_workqueue(priv->workqueue);

	/* ieee80211_unregister_hw calls iwl3945_mac_stop, which flushes
	 * priv->workqueue... so we can't take down the workqueue
	 * until now... */
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;

	free_irq(pdev->irq, priv);
	pci_disable_msi(pdev);

	pci_iounmap(pdev, priv->hw_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	iwl_free_channel_map(priv);
	iwlcore_free_geos(priv);
	kfree(priv->scan);
	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	ieee80211_free_hw(priv->hw);
}

#ifdef CONFIG_PM

static int iwl3945_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);

	if (priv->is_open) {
		set_bit(STATUS_IN_SUSPEND, &priv->status);
		iwl3945_mac_stop(priv->hw);
		priv->is_open = 1;
	}
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int iwl3945_pci_resume(struct pci_dev *pdev)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);
	int ret;

	pci_set_power_state(pdev, PCI_D0);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_restore_state(pdev);

	if (priv->is_open)
		iwl3945_mac_start(priv->hw);

	clear_bit(STATUS_IN_SUSPEND, &priv->status);
	return 0;
}

#endif /* CONFIG_PM */

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
#ifdef CONFIG_PM
	.suspend = iwl3945_pci_suspend,
	.resume = iwl3945_pci_resume,
#endif
};

static int __init iwl3945_init(void)
{

	int ret;
	printk(KERN_INFO DRV_NAME ": " DRV_DESCRIPTION ", " DRV_VERSION "\n");
	printk(KERN_INFO DRV_NAME ": " DRV_COPYRIGHT "\n");

	ret = iwl3945_rate_control_register();
	if (ret) {
		printk(KERN_ERR DRV_NAME
		       "Unable to register rate control algorithm: %d\n", ret);
		return ret;
	}

	ret = pci_register_driver(&iwl3945_driver);
	if (ret) {
		printk(KERN_ERR DRV_NAME "Unable to initialize PCI module\n");
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

module_param_named(antenna, iwl3945_mod_params.antenna, int, 0444);
MODULE_PARM_DESC(antenna, "select antenna (1=Main, 2=Aux, default 0 [both])");
module_param_named(disable, iwl3945_mod_params.disable, int, 0444);
MODULE_PARM_DESC(disable, "manually disable the radio (default 0 [radio on])");
module_param_named(swcrypto, iwl3945_mod_params.sw_crypto, int, 0444);
MODULE_PARM_DESC(swcrypto,
		 "using software crypto (default 1 [software])\n");
module_param_named(debug, iwl3945_mod_params.debug, uint, 0444);
MODULE_PARM_DESC(debug, "debug output mask");
module_param_named(disable_hw_scan, iwl3945_mod_params.disable_hw_scan, int, 0444);
MODULE_PARM_DESC(disable_hw_scan, "disable hardware scanning (default 0)");

module_param_named(queues_num, iwl3945_mod_params.num_of_queues, int, 0444);
MODULE_PARM_DESC(queues_num, "number of hw queues.");

module_param_named(fw_restart3945, iwl3945_mod_params.restart_fw, int, 0444);
MODULE_PARM_DESC(fw_restart3945, "restart firmware in case of error");

module_exit(iwl3945_exit);
module_init(iwl3945_init);
