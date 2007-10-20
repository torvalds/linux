/******************************************************************************
 *
 * Copyright(c) 2003 - 2007 Intel Corporation. All rights reserved.
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
#include <net/mac80211.h>

#include <linux/etherdevice.h>

#define IWL 3945

#include "iwlwifi.h"
#include "iwl-helpers.h"
#include "iwl-3945.h"
#include "iwl-3945-rs.h"

#define IWL_DECLARE_RATE_INFO(r, ip, in, rp, rn, pp, np)    \
	[IWL_RATE_##r##M_INDEX] = { IWL_RATE_##r##M_PLCP,   \
				    IWL_RATE_##r##M_IEEE,   \
				    IWL_RATE_##ip##M_INDEX, \
				    IWL_RATE_##in##M_INDEX, \
				    IWL_RATE_##rp##M_INDEX, \
				    IWL_RATE_##rn##M_INDEX, \
				    IWL_RATE_##pp##M_INDEX, \
				    IWL_RATE_##np##M_INDEX }

/*
 * Parameter order:
 *   rate, prev rate, next rate, prev tgg rate, next tgg rate
 *
 * If there isn't a valid next or previous rate then INV is used which
 * maps to IWL_RATE_INVALID
 *
 */
const struct iwl_rate_info iwl_rates[IWL_RATE_COUNT] = {
	IWL_DECLARE_RATE_INFO(6, 5, 9, 5, 11, 5, 11),        /*  6mbps */
	IWL_DECLARE_RATE_INFO(9, 6, 11, 5, 11, 5, 11),       /*  9mbps */
	IWL_DECLARE_RATE_INFO(12, 11, 18, 11, 18, 11, 18),   /* 12mbps */
	IWL_DECLARE_RATE_INFO(18, 12, 24, 12, 24, 11, 24),   /* 18mbps */
	IWL_DECLARE_RATE_INFO(24, 18, 36, 18, 36, 18, 36),   /* 24mbps */
	IWL_DECLARE_RATE_INFO(36, 24, 48, 24, 48, 24, 48),   /* 36mbps */
	IWL_DECLARE_RATE_INFO(48, 36, 54, 36, 54, 36, 54),   /* 48mbps */
	IWL_DECLARE_RATE_INFO(54, 48, INV, 48, INV, 48, INV),/* 54mbps */
	IWL_DECLARE_RATE_INFO(1, INV, 2, INV, 2, INV, 2),    /*  1mbps */
	IWL_DECLARE_RATE_INFO(2, 1, 5, 1, 5, 1, 5),          /*  2mbps */
	IWL_DECLARE_RATE_INFO(5, 2, 6, 2, 11, 2, 11),        /*5.5mbps */
	IWL_DECLARE_RATE_INFO(11, 9, 12, 5, 12, 5, 18),      /* 11mbps */
};

/* 1 = enable the iwl_disable_events() function */
#define IWL_EVT_DISABLE (0)
#define IWL_EVT_DISABLE_SIZE (1532/32)

/**
 * iwl_disable_events - Disable selected events in uCode event log
 *
 * Disable an event by writing "1"s into "disable"
 *   bitmap in SRAM.  Bit position corresponds to Event # (id/type).
 *   Default values of 0 enable uCode events to be logged.
 * Use for only special debugging.  This function is just a placeholder as-is,
 *   you'll need to provide the special bits! ...
 *   ... and set IWL_EVT_DISABLE to 1. */
void iwl_disable_events(struct iwl_priv *priv)
{
	int rc;
	int i;
	u32 base;		/* SRAM address of event log header */
	u32 disable_ptr;	/* SRAM address of event-disable bitmap array */
	u32 array_size;		/* # of u32 entries in array */
	u32 evt_disable[IWL_EVT_DISABLE_SIZE] = {
		0x00000000,	/*   31 -    0  Event id numbers */
		0x00000000,	/*   63 -   32 */
		0x00000000,	/*   95 -   64 */
		0x00000000,	/*  127 -   96 */
		0x00000000,	/*  159 -  128 */
		0x00000000,	/*  191 -  160 */
		0x00000000,	/*  223 -  192 */
		0x00000000,	/*  255 -  224 */
		0x00000000,	/*  287 -  256 */
		0x00000000,	/*  319 -  288 */
		0x00000000,	/*  351 -  320 */
		0x00000000,	/*  383 -  352 */
		0x00000000,	/*  415 -  384 */
		0x00000000,	/*  447 -  416 */
		0x00000000,	/*  479 -  448 */
		0x00000000,	/*  511 -  480 */
		0x00000000,	/*  543 -  512 */
		0x00000000,	/*  575 -  544 */
		0x00000000,	/*  607 -  576 */
		0x00000000,	/*  639 -  608 */
		0x00000000,	/*  671 -  640 */
		0x00000000,	/*  703 -  672 */
		0x00000000,	/*  735 -  704 */
		0x00000000,	/*  767 -  736 */
		0x00000000,	/*  799 -  768 */
		0x00000000,	/*  831 -  800 */
		0x00000000,	/*  863 -  832 */
		0x00000000,	/*  895 -  864 */
		0x00000000,	/*  927 -  896 */
		0x00000000,	/*  959 -  928 */
		0x00000000,	/*  991 -  960 */
		0x00000000,	/* 1023 -  992 */
		0x00000000,	/* 1055 - 1024 */
		0x00000000,	/* 1087 - 1056 */
		0x00000000,	/* 1119 - 1088 */
		0x00000000,	/* 1151 - 1120 */
		0x00000000,	/* 1183 - 1152 */
		0x00000000,	/* 1215 - 1184 */
		0x00000000,	/* 1247 - 1216 */
		0x00000000,	/* 1279 - 1248 */
		0x00000000,	/* 1311 - 1280 */
		0x00000000,	/* 1343 - 1312 */
		0x00000000,	/* 1375 - 1344 */
		0x00000000,	/* 1407 - 1376 */
		0x00000000,	/* 1439 - 1408 */
		0x00000000,	/* 1471 - 1440 */
		0x00000000,	/* 1503 - 1472 */
	};

	base = le32_to_cpu(priv->card_alive.log_event_table_ptr);
	if (!iwl_hw_valid_rtc_data_addr(base)) {
		IWL_ERROR("Invalid event log pointer 0x%08X\n", base);
		return;
	}

	rc = iwl_grab_restricted_access(priv);
	if (rc) {
		IWL_WARNING("Can not read from adapter at this time.\n");
		return;
	}

	disable_ptr = iwl_read_restricted_mem(priv, base + (4 * sizeof(u32)));
	array_size = iwl_read_restricted_mem(priv, base + (5 * sizeof(u32)));
	iwl_release_restricted_access(priv);

	if (IWL_EVT_DISABLE && (array_size == IWL_EVT_DISABLE_SIZE)) {
		IWL_DEBUG_INFO("Disabling selected uCode log events at 0x%x\n",
			       disable_ptr);
		rc = iwl_grab_restricted_access(priv);
		for (i = 0; i < IWL_EVT_DISABLE_SIZE; i++)
			iwl_write_restricted_mem(priv,
						 disable_ptr +
						 (i * sizeof(u32)),
						 evt_disable[i]);

		iwl_release_restricted_access(priv);
	} else {
		IWL_DEBUG_INFO("Selected uCode log events may be disabled\n");
		IWL_DEBUG_INFO("  by writing \"1\"s into disable bitmap\n");
		IWL_DEBUG_INFO("  in SRAM at 0x%x, size %d u32s\n",
			       disable_ptr, array_size);
	}

}

/**
 * iwl3945_get_antenna_flags - Get antenna flags for RXON command
 * @priv: eeprom and antenna fields are used to determine antenna flags
 *
 * priv->eeprom  is used to determine if antenna AUX/MAIN are reversed
 * priv->antenna specifies the antenna diversity mode:
 *
 * IWL_ANTENNA_DIVERISTY - NIC selects best antenna by itself
 * IWL_ANTENNA_MAIN      - Force MAIN antenna
 * IWL_ANTENNA_AUX       - Force AUX antenna
 */
__le32 iwl3945_get_antenna_flags(const struct iwl_priv *priv)
{
	switch (priv->antenna) {
	case IWL_ANTENNA_DIVERSITY:
		return 0;

	case IWL_ANTENNA_MAIN:
		if (priv->eeprom.antenna_switch_type)
			return RXON_FLG_DIS_DIV_MSK | RXON_FLG_ANT_B_MSK;
		return RXON_FLG_DIS_DIV_MSK | RXON_FLG_ANT_A_MSK;

	case IWL_ANTENNA_AUX:
		if (priv->eeprom.antenna_switch_type)
			return RXON_FLG_DIS_DIV_MSK | RXON_FLG_ANT_A_MSK;
		return RXON_FLG_DIS_DIV_MSK | RXON_FLG_ANT_B_MSK;
	}

	/* bad antenna selector value */
	IWL_ERROR("Bad antenna selector value (0x%x)\n", priv->antenna);
	return 0;		/* "diversity" is default if error */
}

/*****************************************************************************
 *
 * Intel PRO/Wireless 3945ABG/BG Network Connection
 *
 *  RX handler implementations
 *
 *  Used by iwl-base.c
 *
 *****************************************************************************/

void iwl_hw_rx_statistics(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (void *)rxb->skb->data;
	IWL_DEBUG_RX("Statistics notification received (%d vs %d).\n",
		     (int)sizeof(struct iwl_notif_statistics),
		     le32_to_cpu(pkt->len));

	memcpy(&priv->statistics, pkt->u.raw, sizeof(priv->statistics));

	priv->last_statistics_time = jiffies;
}

static void iwl3945_handle_data_packet(struct iwl_priv *priv, int is_data,
				   struct iwl_rx_mem_buffer *rxb,
				   struct ieee80211_rx_status *stats,
				   u16 phy_flags)
{
	struct ieee80211_hdr *hdr;
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl_rx_frame_hdr *rx_hdr = IWL_RX_HDR(pkt);
	struct iwl_rx_frame_end *rx_end = IWL_RX_END(pkt);
	short len = le16_to_cpu(rx_hdr->len);

	/* We received data from the HW, so stop the watchdog */
	if (unlikely((len + IWL_RX_FRAME_SIZE) > skb_tailroom(rxb->skb))) {
		IWL_DEBUG_DROP("Corruption detected!\n");
		return;
	}

	/* We only process data packets if the interface is open */
	if (unlikely(!priv->is_open)) {
		IWL_DEBUG_DROP_LIMIT
		    ("Dropping packet while interface is not open.\n");
		return;
	}
	if (priv->iw_mode == IEEE80211_IF_TYPE_MNTR) {
		if (iwl_param_hwcrypto)
			iwl_set_decrypted_flag(priv, rxb->skb,
					       le32_to_cpu(rx_end->status),
					       stats);
		iwl_handle_data_packet_monitor(priv, rxb, IWL_RX_DATA(pkt),
					       len, stats, phy_flags);
		return;
	}

	skb_reserve(rxb->skb, (void *)rx_hdr->payload - (void *)pkt);
	/* Set the size of the skb to the size of the frame */
	skb_put(rxb->skb, le16_to_cpu(rx_hdr->len));

	hdr = (void *)rxb->skb->data;

	if (iwl_param_hwcrypto)
		iwl_set_decrypted_flag(priv, rxb->skb,
				       le32_to_cpu(rx_end->status), stats);

	ieee80211_rx_irqsafe(priv->hw, rxb->skb, stats);
	rxb->skb = NULL;
}

static void iwl3945_rx_reply_rx(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl_rx_frame_stats *rx_stats = IWL_RX_STATS(pkt);
	struct iwl_rx_frame_hdr *rx_hdr = IWL_RX_HDR(pkt);
	struct iwl_rx_frame_end *rx_end = IWL_RX_END(pkt);
	struct ieee80211_hdr *header;
	u16 phy_flags = le16_to_cpu(rx_hdr->phy_flags);
	u16 rx_stats_sig_avg = le16_to_cpu(rx_stats->sig_avg);
	u16 rx_stats_noise_diff = le16_to_cpu(rx_stats->noise_diff);
	struct ieee80211_rx_status stats = {
		.mactime = le64_to_cpu(rx_end->timestamp),
		.freq = ieee80211chan2mhz(le16_to_cpu(rx_hdr->channel)),
		.channel = le16_to_cpu(rx_hdr->channel),
		.phymode = (rx_hdr->phy_flags & RX_RES_PHY_FLAGS_BAND_24_MSK) ?
		MODE_IEEE80211G : MODE_IEEE80211A,
		.antenna = 0,
		.rate = rx_hdr->rate,
		.flag = 0,
	};
	u8 network_packet;
	int snr;

	if ((unlikely(rx_stats->phy_count > 20))) {
		IWL_DEBUG_DROP
		    ("dsp size out of range [0,20]: "
		     "%d/n", rx_stats->phy_count);
		return;
	}

	if (!(rx_end->status & RX_RES_STATUS_NO_CRC32_ERROR)
	    || !(rx_end->status & RX_RES_STATUS_NO_RXE_OVERFLOW)) {
		IWL_DEBUG_RX("Bad CRC or FIFO: 0x%08X.\n", rx_end->status);
		return;
	}

	if (priv->iw_mode == IEEE80211_IF_TYPE_MNTR) {
		iwl3945_handle_data_packet(priv, 1, rxb, &stats, phy_flags);
		return;
	}

	/* Convert 3945's rssi indicator to dBm */
	stats.ssi = rx_stats->rssi - IWL_RSSI_OFFSET;

	/* Set default noise value to -127 */
	if (priv->last_rx_noise == 0)
		priv->last_rx_noise = IWL_NOISE_MEAS_NOT_AVAILABLE;

	/* 3945 provides noise info for OFDM frames only.
	 * sig_avg and noise_diff are measured by the 3945's digital signal
	 *   processor (DSP), and indicate linear levels of signal level and
	 *   distortion/noise within the packet preamble after
	 *   automatic gain control (AGC).  sig_avg should stay fairly
	 *   constant if the radio's AGC is working well.
	 * Since these values are linear (not dB or dBm), linear
	 *   signal-to-noise ratio (SNR) is (sig_avg / noise_diff).
	 * Convert linear SNR to dB SNR, then subtract that from rssi dBm
	 *   to obtain noise level in dBm.
	 * Calculate stats.signal (quality indicator in %) based on SNR. */
	if (rx_stats_noise_diff) {
		snr = rx_stats_sig_avg / rx_stats_noise_diff;
		stats.noise = stats.ssi - iwl_calc_db_from_ratio(snr);
		stats.signal = iwl_calc_sig_qual(stats.ssi, stats.noise);

	/* If noise info not available, calculate signal quality indicator (%)
	 *   using just the dBm signal level. */
	} else {
		stats.noise = priv->last_rx_noise;
		stats.signal = iwl_calc_sig_qual(stats.ssi, 0);
	}


	IWL_DEBUG_STATS("Rssi %d noise %d qual %d sig_avg %d noise_diff %d\n",
			stats.ssi, stats.noise, stats.signal,
			rx_stats_sig_avg, rx_stats_noise_diff);

	stats.freq = ieee80211chan2mhz(stats.channel);

	/* can be covered by iwl_report_frame() in most cases */
/*      IWL_DEBUG_RX("RX status: 0x%08X\n", rx_end->status); */

	header = (struct ieee80211_hdr *)IWL_RX_DATA(pkt);

	network_packet = iwl_is_network_packet(priv, header);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_debug_level & IWL_DL_STATS && net_ratelimit())
		IWL_DEBUG_STATS
		    ("[%c] %d RSSI: %d Signal: %u, Noise: %u, Rate: %u\n",
		     network_packet ? '*' : ' ',
		     stats.channel, stats.ssi, stats.ssi,
		     stats.ssi, stats.rate);

	if (iwl_debug_level & (IWL_DL_RX))
		/* Set "1" to report good data frames in groups of 100 */
		iwl_report_frame(priv, pkt, header, 1);
#endif

	if (network_packet) {
		priv->last_beacon_time = le32_to_cpu(rx_end->beacon_timestamp);
		priv->last_tsf = le64_to_cpu(rx_end->timestamp);
		priv->last_rx_rssi = stats.ssi;
		priv->last_rx_noise = stats.noise;
	}

	switch (le16_to_cpu(header->frame_control) & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_MGMT:
		switch (le16_to_cpu(header->frame_control) &
			IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_PROBE_RESP:
		case IEEE80211_STYPE_BEACON:{
				/* If this is a beacon or probe response for
				 * our network then cache the beacon
				 * timestamp */
				if ((((priv->iw_mode == IEEE80211_IF_TYPE_STA)
				      && !compare_ether_addr(header->addr2,
							     priv->bssid)) ||
				     ((priv->iw_mode == IEEE80211_IF_TYPE_IBSS)
				      && !compare_ether_addr(header->addr3,
							     priv->bssid)))) {
					struct ieee80211_mgmt *mgmt =
					    (struct ieee80211_mgmt *)header;
					__le32 *pos;
					pos =
					    (__le32 *) & mgmt->u.beacon.
					    timestamp;
					priv->timestamp0 = le32_to_cpu(pos[0]);
					priv->timestamp1 = le32_to_cpu(pos[1]);
					priv->beacon_int = le16_to_cpu(
					    mgmt->u.beacon.beacon_int);
					if (priv->call_post_assoc_from_beacon &&
					    (priv->iw_mode ==
						IEEE80211_IF_TYPE_STA))
						queue_work(priv->workqueue,
						    &priv->post_associate.work);

					priv->call_post_assoc_from_beacon = 0;
				}

				break;
			}

		case IEEE80211_STYPE_ACTION:
			/* TODO: Parse 802.11h frames for CSA... */
			break;

			/*
			 * TODO: There is no callback function from upper
			 * stack to inform us when associated status. this
			 * work around to sniff assoc_resp management frame
			 * and finish the association process.
			 */
		case IEEE80211_STYPE_ASSOC_RESP:
		case IEEE80211_STYPE_REASSOC_RESP:{
				struct ieee80211_mgmt *mgnt =
				    (struct ieee80211_mgmt *)header;
				priv->assoc_id = (~((1 << 15) | (1 << 14)) &
						  le16_to_cpu(mgnt->u.
							      assoc_resp.aid));
				priv->assoc_capability =
				    le16_to_cpu(mgnt->u.assoc_resp.capab_info);
				if (priv->beacon_int)
					queue_work(priv->workqueue,
					    &priv->post_associate.work);
				else
					priv->call_post_assoc_from_beacon = 1;
				break;
			}

		case IEEE80211_STYPE_PROBE_REQ:{
				DECLARE_MAC_BUF(mac1);
				DECLARE_MAC_BUF(mac2);
				DECLARE_MAC_BUF(mac3);
				if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS)
					IWL_DEBUG_DROP
					    ("Dropping (non network): %s"
					     ", %s, %s\n",
					     print_mac(mac1, header->addr1),
					     print_mac(mac2, header->addr2),
					     print_mac(mac3, header->addr3));
				return;
			}
		}

		iwl3945_handle_data_packet(priv, 0, rxb, &stats, phy_flags);
		break;

	case IEEE80211_FTYPE_CTL:
		break;

	case IEEE80211_FTYPE_DATA: {
		DECLARE_MAC_BUF(mac1);
		DECLARE_MAC_BUF(mac2);
		DECLARE_MAC_BUF(mac3);

		if (unlikely(is_duplicate_packet(priv, header)))
			IWL_DEBUG_DROP("Dropping (dup): %s, %s, %s\n",
				       print_mac(mac1, header->addr1),
				       print_mac(mac2, header->addr2),
				       print_mac(mac3, header->addr3));
		else
			iwl3945_handle_data_packet(priv, 1, rxb, &stats,
						   phy_flags);
		break;
	}
	}
}

int iwl_hw_txq_attach_buf_to_tfd(struct iwl_priv *priv, void *ptr,
				 dma_addr_t addr, u16 len)
{
	int count;
	u32 pad;
	struct iwl_tfd_frame *tfd = (struct iwl_tfd_frame *)ptr;

	count = TFD_CTL_COUNT_GET(le32_to_cpu(tfd->control_flags));
	pad = TFD_CTL_PAD_GET(le32_to_cpu(tfd->control_flags));

	if ((count >= NUM_TFD_CHUNKS) || (count < 0)) {
		IWL_ERROR("Error can not send more than %d chunks\n",
			  NUM_TFD_CHUNKS);
		return -EINVAL;
	}

	tfd->pa[count].addr = cpu_to_le32(addr);
	tfd->pa[count].len = cpu_to_le32(len);

	count++;

	tfd->control_flags = cpu_to_le32(TFD_CTL_COUNT_SET(count) |
					 TFD_CTL_PAD_SET(pad));

	return 0;
}

/**
 * iwl_hw_txq_free_tfd - Free one TFD, those at index [txq->q.last_used]
 *
 * Does NOT advance any indexes
 */
int iwl_hw_txq_free_tfd(struct iwl_priv *priv, struct iwl_tx_queue *txq)
{
	struct iwl_tfd_frame *bd_tmp = (struct iwl_tfd_frame *)&txq->bd[0];
	struct iwl_tfd_frame *bd = &bd_tmp[txq->q.last_used];
	struct pci_dev *dev = priv->pci_dev;
	int i;
	int counter;

	/* classify bd */
	if (txq->q.id == IWL_CMD_QUEUE_NUM)
		/* nothing to cleanup after for host commands */
		return 0;

	/* sanity check */
	counter = TFD_CTL_COUNT_GET(le32_to_cpu(bd->control_flags));
	if (counter > NUM_TFD_CHUNKS) {
		IWL_ERROR("Too many chunks: %i\n", counter);
		/* @todo issue fatal error, it is quite serious situation */
		return 0;
	}

	/* unmap chunks if any */

	for (i = 1; i < counter; i++) {
		pci_unmap_single(dev, le32_to_cpu(bd->pa[i].addr),
				 le32_to_cpu(bd->pa[i].len), PCI_DMA_TODEVICE);
		if (txq->txb[txq->q.last_used].skb[0]) {
			struct sk_buff *skb = txq->txb[txq->q.last_used].skb[0];
			if (txq->txb[txq->q.last_used].skb[0]) {
				/* Can be called from interrupt context */
				dev_kfree_skb_any(skb);
				txq->txb[txq->q.last_used].skb[0] = NULL;
			}
		}
	}
	return 0;
}

u8 iwl_hw_find_station(struct iwl_priv *priv, const u8 *addr)
{
	int i;
	int ret = IWL_INVALID_STATION;
	unsigned long flags;
	DECLARE_MAC_BUF(mac);

	spin_lock_irqsave(&priv->sta_lock, flags);
	for (i = IWL_STA_ID; i < priv->hw_setting.max_stations; i++)
		if ((priv->stations[i].used) &&
		    (!compare_ether_addr
		     (priv->stations[i].sta.sta.addr, addr))) {
			ret = i;
			goto out;
		}

	IWL_DEBUG_INFO("can not find STA %s (total %d)\n",
		       print_mac(mac, addr), priv->num_stations);
 out:
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return ret;
}

/**
 * iwl_hw_build_tx_cmd_rate - Add rate portion to TX_CMD:
 *
*/
void iwl_hw_build_tx_cmd_rate(struct iwl_priv *priv,
			      struct iwl_cmd *cmd,
			      struct ieee80211_tx_control *ctrl,
			      struct ieee80211_hdr *hdr, int sta_id, int tx_id)
{
	unsigned long flags;
	u16 rate_index = min(ctrl->tx_rate & 0xffff, IWL_RATE_COUNT - 1);
	u16 rate_mask;
	int rate;
	u8 rts_retry_limit;
	u8 data_retry_limit;
	__le32 tx_flags;
	u16 fc = le16_to_cpu(hdr->frame_control);

	rate = iwl_rates[rate_index].plcp;
	tx_flags = cmd->cmd.tx.tx_flags;

	/* We need to figure out how to get the sta->supp_rates while
	 * in this running context; perhaps encoding into ctrl->tx_rate? */
	rate_mask = IWL_RATES_MASK;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].current_rate.rate_n_flags = rate;

	if ((priv->iw_mode == IEEE80211_IF_TYPE_IBSS) &&
	    (sta_id != IWL3945_BROADCAST_ID) &&
		(sta_id != IWL_MULTICAST_ID))
		priv->stations[IWL_STA_ID].current_rate.rate_n_flags = rate;

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	if (tx_id >= IWL_CMD_QUEUE_NUM)
		rts_retry_limit = 3;
	else
		rts_retry_limit = 7;

	if (ieee80211_is_probe_response(fc)) {
		data_retry_limit = 3;
		if (data_retry_limit < rts_retry_limit)
			rts_retry_limit = data_retry_limit;
	} else
		data_retry_limit = IWL_DEFAULT_TX_RETRY;

	if (priv->data_retry_limit != -1)
		data_retry_limit = priv->data_retry_limit;

	if ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) {
		switch (fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_AUTH:
		case IEEE80211_STYPE_DEAUTH:
		case IEEE80211_STYPE_ASSOC_REQ:
		case IEEE80211_STYPE_REASSOC_REQ:
			if (tx_flags & TX_CMD_FLG_RTS_MSK) {
				tx_flags &= ~TX_CMD_FLG_RTS_MSK;
				tx_flags |= TX_CMD_FLG_CTS_MSK;
			}
			break;
		default:
			break;
		}
	}

	cmd->cmd.tx.rts_retry_limit = rts_retry_limit;
	cmd->cmd.tx.data_retry_limit = data_retry_limit;
	cmd->cmd.tx.rate = rate;
	cmd->cmd.tx.tx_flags = tx_flags;

	/* OFDM */
	cmd->cmd.tx.supp_rates[0] = rate_mask & IWL_OFDM_RATES_MASK;

	/* CCK */
	cmd->cmd.tx.supp_rates[1] = (rate_mask >> 8) & 0xF;

	IWL_DEBUG_RATE("Tx sta id: %d, rate: %d (plcp), flags: 0x%4X "
		       "cck/ofdm mask: 0x%x/0x%x\n", sta_id,
		       cmd->cmd.tx.rate, le32_to_cpu(cmd->cmd.tx.tx_flags),
		       cmd->cmd.tx.supp_rates[1], cmd->cmd.tx.supp_rates[0]);
}

u8 iwl3945_sync_sta(struct iwl_priv *priv, int sta_id, u16 tx_rate, u8 flags)
{
	unsigned long flags_spin;
	struct iwl_station_entry *station;

	if (sta_id == IWL_INVALID_STATION)
		return IWL_INVALID_STATION;

	spin_lock_irqsave(&priv->sta_lock, flags_spin);
	station = &priv->stations[sta_id];

	station->sta.sta.modify_mask = STA_MODIFY_TX_RATE_MSK;
	station->sta.rate_n_flags = cpu_to_le16(tx_rate);
	station->current_rate.rate_n_flags = tx_rate;
	station->sta.mode = STA_CONTROL_MODIFY_MSK;

	spin_unlock_irqrestore(&priv->sta_lock, flags_spin);

	iwl_send_add_station(priv, &station->sta, flags);
	IWL_DEBUG_RATE("SCALE sync station %d to rate %d\n",
			sta_id, tx_rate);
	return sta_id;
}

void iwl_hw_card_show_info(struct iwl_priv *priv)
{
	IWL_DEBUG_INFO("3945ABG HW Version %u.%u.%u\n",
		       ((priv->eeprom.board_revision >> 8) & 0x0F),
		       ((priv->eeprom.board_revision >> 8) >> 4),
		       (priv->eeprom.board_revision & 0x00FF));

	IWL_DEBUG_INFO("3945ABG PBA Number %.*s\n",
		       (int)sizeof(priv->eeprom.board_pba_number),
		       priv->eeprom.board_pba_number);

	IWL_DEBUG_INFO("EEPROM_ANTENNA_SWITCH_TYPE is 0x%02X\n",
		       priv->eeprom.antenna_switch_type);
}

static int iwl3945_nic_set_pwr_src(struct iwl_priv *priv, int pwr_max)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	rc = iwl_grab_restricted_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}

	if (!pwr_max) {
		u32 val;

		rc = pci_read_config_dword(priv->pci_dev,
				PCI_POWER_SOURCE, &val);
		if (val & PCI_CFG_PMC_PME_FROM_D3COLD_SUPPORT) {
			iwl_set_bits_mask_restricted_reg(priv, APMG_PS_CTRL_REG,
					APMG_PS_CTRL_VAL_PWR_SRC_VAUX,
					~APMG_PS_CTRL_MSK_PWR_SRC);
			iwl_release_restricted_access(priv);

			iwl_poll_bit(priv, CSR_GPIO_IN,
				     CSR_GPIO_IN_VAL_VAUX_PWR_SRC,
				     CSR_GPIO_IN_BIT_AUX_POWER, 5000);
		} else
			iwl_release_restricted_access(priv);
	} else {
		iwl_set_bits_mask_restricted_reg(priv, APMG_PS_CTRL_REG,
				APMG_PS_CTRL_VAL_PWR_SRC_VMAIN,
				~APMG_PS_CTRL_MSK_PWR_SRC);

		iwl_release_restricted_access(priv);
		iwl_poll_bit(priv, CSR_GPIO_IN, CSR_GPIO_IN_VAL_VMAIN_PWR_SRC,
			     CSR_GPIO_IN_BIT_AUX_POWER, 5000);	/* uS */
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	return rc;
}

static int iwl3945_rx_init(struct iwl_priv *priv, struct iwl_rx_queue *rxq)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	rc = iwl_grab_restricted_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}

	iwl_write_restricted(priv, FH_RCSR_RBD_BASE(0), rxq->dma_addr);
	iwl_write_restricted(priv, FH_RCSR_RPTR_ADDR(0),
			     priv->hw_setting.shared_phys +
			     offsetof(struct iwl_shared, rx_read_ptr[0]));
	iwl_write_restricted(priv, FH_RCSR_WPTR(0), 0);
	iwl_write_restricted(priv, FH_RCSR_CONFIG(0),
		ALM_FH_RCSR_RX_CONFIG_REG_VAL_DMA_CHNL_EN_ENABLE |
		ALM_FH_RCSR_RX_CONFIG_REG_VAL_RDRBD_EN_ENABLE |
		ALM_FH_RCSR_RX_CONFIG_REG_BIT_WR_STTS_EN |
		ALM_FH_RCSR_RX_CONFIG_REG_VAL_MAX_FRAG_SIZE_128 |
		(RX_QUEUE_SIZE_LOG << ALM_FH_RCSR_RX_CONFIG_REG_POS_RBDC_SIZE) |
		ALM_FH_RCSR_RX_CONFIG_REG_VAL_IRQ_DEST_INT_HOST |
		(1 << ALM_FH_RCSR_RX_CONFIG_REG_POS_IRQ_RBTH) |
		ALM_FH_RCSR_RX_CONFIG_REG_VAL_MSG_MODE_FH);

	/* fake read to flush all prev I/O */
	iwl_read_restricted(priv, FH_RSSR_CTRL);

	iwl_release_restricted_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int iwl3945_tx_reset(struct iwl_priv *priv)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	rc = iwl_grab_restricted_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}

	/* bypass mode */
	iwl_write_restricted_reg(priv, SCD_MODE_REG, 0x2);

	/* RA 0 is active */
	iwl_write_restricted_reg(priv, SCD_ARASTAT_REG, 0x01);

	/* all 6 fifo are active */
	iwl_write_restricted_reg(priv, SCD_TXFACT_REG, 0x3f);

	iwl_write_restricted_reg(priv, SCD_SBYP_MODE_1_REG, 0x010000);
	iwl_write_restricted_reg(priv, SCD_SBYP_MODE_2_REG, 0x030002);
	iwl_write_restricted_reg(priv, SCD_TXF4MF_REG, 0x000004);
	iwl_write_restricted_reg(priv, SCD_TXF5MF_REG, 0x000005);

	iwl_write_restricted(priv, FH_TSSR_CBB_BASE,
			     priv->hw_setting.shared_phys);

	iwl_write_restricted(priv, FH_TSSR_MSG_CONFIG,
		ALM_FH_TSSR_TX_MSG_CONFIG_REG_VAL_SNOOP_RD_TXPD_ON |
		ALM_FH_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RD_TXPD_ON |
		ALM_FH_TSSR_TX_MSG_CONFIG_REG_VAL_MAX_FRAG_SIZE_128B |
		ALM_FH_TSSR_TX_MSG_CONFIG_REG_VAL_SNOOP_RD_TFD_ON |
		ALM_FH_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RD_CBB_ON |
		ALM_FH_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RSP_WAIT_TH |
		ALM_FH_TSSR_TX_MSG_CONFIG_REG_VAL_RSP_WAIT_TH);

	iwl_release_restricted_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

/**
 * iwl3945_txq_ctx_reset - Reset TX queue context
 *
 * Destroys all DMA structures and initialize them again
 */
static int iwl3945_txq_ctx_reset(struct iwl_priv *priv)
{
	int rc;
	int txq_id, slots_num;

	iwl_hw_txq_ctx_free(priv);

	/* Tx CMD queue */
	rc = iwl3945_tx_reset(priv);
	if (rc)
		goto error;

	/* Tx queue(s) */
	for (txq_id = 0; txq_id < TFD_QUEUE_MAX; txq_id++) {
		slots_num = (txq_id == IWL_CMD_QUEUE_NUM) ?
				TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
		rc = iwl_tx_queue_init(priv, &priv->txq[txq_id], slots_num,
				txq_id);
		if (rc) {
			IWL_ERROR("Tx %d queue init failed\n", txq_id);
			goto error;
		}
	}

	return rc;

 error:
	iwl_hw_txq_ctx_free(priv);
	return rc;
}

int iwl_hw_nic_init(struct iwl_priv *priv)
{
	u8 rev_id;
	int rc;
	unsigned long flags;
	struct iwl_rx_queue *rxq = &priv->rxq;

	iwl_power_init_handle(priv);

	spin_lock_irqsave(&priv->lock, flags);
	iwl_set_bit(priv, CSR_ANA_PLL_CFG, (1 << 24));
	iwl_set_bit(priv, CSR_GIO_CHICKEN_BITS,
		    CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);

	iwl_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
	rc = iwl_poll_bit(priv, CSR_GP_CNTRL,
			  CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			  CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (rc < 0) {
		spin_unlock_irqrestore(&priv->lock, flags);
		IWL_DEBUG_INFO("Failed to init the card\n");
		return rc;
	}

	rc = iwl_grab_restricted_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}
	iwl_write_restricted_reg(priv, APMG_CLK_EN_REG,
				 APMG_CLK_VAL_DMA_CLK_RQT |
				 APMG_CLK_VAL_BSM_CLK_RQT);
	udelay(20);
	iwl_set_bits_restricted_reg(priv, APMG_PCIDEV_STT_REG,
				    APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
	iwl_release_restricted_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Determine HW type */
	rc = pci_read_config_byte(priv->pci_dev, PCI_REVISION_ID, &rev_id);
	if (rc)
		return rc;
	IWL_DEBUG_INFO("HW Revision ID = 0x%X\n", rev_id);

	iwl3945_nic_set_pwr_src(priv, 1);
	spin_lock_irqsave(&priv->lock, flags);

	if (rev_id & PCI_CFG_REV_ID_BIT_RTP)
		IWL_DEBUG_INFO("RTP type \n");
	else if (rev_id & PCI_CFG_REV_ID_BIT_BASIC_SKU) {
		IWL_DEBUG_INFO("ALM-MB type\n");
		iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_BIT_ALMAGOR_MB);
	} else {
		IWL_DEBUG_INFO("ALM-MM type\n");
		iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_BIT_ALMAGOR_MM);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	/* Initialize the EEPROM */
	rc = iwl_eeprom_init(priv);
	if (rc)
		return rc;

	spin_lock_irqsave(&priv->lock, flags);
	if (EEPROM_SKU_CAP_OP_MODE_MRC == priv->eeprom.sku_cap) {
		IWL_DEBUG_INFO("SKU OP mode is mrc\n");
		iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_BIT_SKU_MRC);
	} else
		IWL_DEBUG_INFO("SKU OP mode is basic\n");

	if ((priv->eeprom.board_revision & 0xF0) == 0xD0) {
		IWL_DEBUG_INFO("3945ABG revision is 0x%X\n",
			       priv->eeprom.board_revision);
		iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_BIT_BOARD_TYPE);
	} else {
		IWL_DEBUG_INFO("3945ABG revision is 0x%X\n",
			       priv->eeprom.board_revision);
		iwl_clear_bit(priv, CSR_HW_IF_CONFIG_REG,
			      CSR_HW_IF_CONFIG_REG_BIT_BOARD_TYPE);
	}

	if (priv->eeprom.almgor_m_version <= 1) {
		iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_BITS_SILICON_TYPE_A);
		IWL_DEBUG_INFO("Card M type A version is 0x%X\n",
			       priv->eeprom.almgor_m_version);
	} else {
		IWL_DEBUG_INFO("Card M type B version is 0x%X\n",
			       priv->eeprom.almgor_m_version);
		iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_BITS_SILICON_TYPE_B);
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	if (priv->eeprom.sku_cap & EEPROM_SKU_CAP_SW_RF_KILL_ENABLE)
		IWL_DEBUG_RF_KILL("SW RF KILL supported in EEPROM.\n");

	if (priv->eeprom.sku_cap & EEPROM_SKU_CAP_HW_RF_KILL_ENABLE)
		IWL_DEBUG_RF_KILL("HW RF KILL supported in EEPROM.\n");

	/* Allocate the RX queue, or reset if it is already allocated */
	if (!rxq->bd) {
		rc = iwl_rx_queue_alloc(priv);
		if (rc) {
			IWL_ERROR("Unable to initialize Rx queue\n");
			return -ENOMEM;
		}
	} else
		iwl_rx_queue_reset(priv, rxq);

	iwl_rx_replenish(priv);

	iwl3945_rx_init(priv, rxq);

	spin_lock_irqsave(&priv->lock, flags);

	/* Look at using this instead:
	rxq->need_update = 1;
	iwl_rx_queue_update_write_ptr(priv, rxq);
	*/

	rc = iwl_grab_restricted_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}
	iwl_write_restricted(priv, FH_RCSR_WPTR(0), rxq->write & ~7);
	iwl_release_restricted_access(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	rc = iwl3945_txq_ctx_reset(priv);
	if (rc)
		return rc;

	set_bit(STATUS_INIT, &priv->status);

	return 0;
}

/**
 * iwl_hw_txq_ctx_free - Free TXQ Context
 *
 * Destroy all TX DMA queues and structures
 */
void iwl_hw_txq_ctx_free(struct iwl_priv *priv)
{
	int txq_id;

	/* Tx queues */
	for (txq_id = 0; txq_id < TFD_QUEUE_MAX; txq_id++)
		iwl_tx_queue_free(priv, &priv->txq[txq_id]);
}

void iwl_hw_txq_ctx_stop(struct iwl_priv *priv)
{
	int queue;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (iwl_grab_restricted_access(priv)) {
		spin_unlock_irqrestore(&priv->lock, flags);
		iwl_hw_txq_ctx_free(priv);
		return;
	}

	/* stop SCD */
	iwl_write_restricted_reg(priv, SCD_MODE_REG, 0);

	/* reset TFD queues */
	for (queue = TFD_QUEUE_MIN; queue < TFD_QUEUE_MAX; queue++) {
		iwl_write_restricted(priv, FH_TCSR_CONFIG(queue), 0x0);
		iwl_poll_restricted_bit(priv, FH_TSSR_TX_STATUS,
				ALM_FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(queue),
				1000);
	}

	iwl_release_restricted_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_hw_txq_ctx_free(priv);
}

int iwl_hw_nic_stop_master(struct iwl_priv *priv)
{
	int rc = 0;
	u32 reg_val;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	/* set stop master bit */
	iwl_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_STOP_MASTER);

	reg_val = iwl_read32(priv, CSR_GP_CNTRL);

	if (CSR_GP_CNTRL_REG_FLAG_MAC_POWER_SAVE ==
	    (reg_val & CSR_GP_CNTRL_REG_MSK_POWER_SAVE_TYPE))
		IWL_DEBUG_INFO("Card in power save, master is already "
			       "stopped\n");
	else {
		rc = iwl_poll_bit(priv, CSR_RESET,
				  CSR_RESET_REG_FLAG_MASTER_DISABLED,
				  CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
		if (rc < 0) {
			spin_unlock_irqrestore(&priv->lock, flags);
			return rc;
		}
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	IWL_DEBUG_INFO("stop master\n");

	return rc;
}

int iwl_hw_nic_reset(struct iwl_priv *priv)
{
	int rc;
	unsigned long flags;

	iwl_hw_nic_stop_master(priv);

	spin_lock_irqsave(&priv->lock, flags);

	iwl_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	rc = iwl_poll_bit(priv, CSR_GP_CNTRL,
			  CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			  CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);

	rc = iwl_grab_restricted_access(priv);
	if (!rc) {
		iwl_write_restricted_reg(priv, APMG_CLK_CTRL_REG,
					 APMG_CLK_VAL_BSM_CLK_RQT);

		udelay(10);

		iwl_set_bit(priv, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

		iwl_write_restricted_reg(priv, APMG_RTC_INT_MSK_REG, 0x0);
		iwl_write_restricted_reg(priv, APMG_RTC_INT_STT_REG,
					0xFFFFFFFF);

		/* enable DMA */
		iwl_write_restricted_reg(priv, APMG_CLK_EN_REG,
					 APMG_CLK_VAL_DMA_CLK_RQT |
					 APMG_CLK_VAL_BSM_CLK_RQT);
		udelay(10);

		iwl_set_bits_restricted_reg(priv, APMG_PS_CTRL_REG,
				APMG_PS_CTRL_VAL_RESET_REQ);
		udelay(5);
		iwl_clear_bits_restricted_reg(priv, APMG_PS_CTRL_REG,
				APMG_PS_CTRL_VAL_RESET_REQ);
		iwl_release_restricted_access(priv);
	}

	/* Clear the 'host command active' bit... */
	clear_bit(STATUS_HCMD_ACTIVE, &priv->status);

	wake_up_interruptible(&priv->wait_command_queue);
	spin_unlock_irqrestore(&priv->lock, flags);

	return rc;
}

/**
 * iwl_hw_reg_adjust_power_by_temp - return index delta into power gain settings table
 */
static int iwl_hw_reg_adjust_power_by_temp(int new_reading, int old_reading)
{
	return (new_reading - old_reading) * (-11) / 100;
}

/**
 * iwl_hw_reg_temp_out_of_range - Keep temperature in sane range
 */
static inline int iwl_hw_reg_temp_out_of_range(int temperature)
{
	return (((temperature < -260) || (temperature > 25)) ? 1 : 0);
}

int iwl_hw_get_temperature(struct iwl_priv *priv)
{
	return iwl_read32(priv, CSR_UCODE_DRV_GP2);
}

/**
 * iwl_hw_reg_txpower_get_temperature - get current temperature by reading from NIC
 */
static int iwl_hw_reg_txpower_get_temperature(struct iwl_priv *priv)
{
	int temperature;

	temperature = iwl_hw_get_temperature(priv);

	/* driver's okay range is -260 to +25.
	 *   human readable okay range is 0 to +285 */
	IWL_DEBUG_INFO("Temperature: %d\n", temperature + IWL_TEMP_CONVERT);

	/* handle insane temp reading */
	if (iwl_hw_reg_temp_out_of_range(temperature)) {
		IWL_ERROR("Error bad temperature value  %d\n", temperature);

		/* if really really hot(?),
		 *   substitute the 3rd band/group's temp measured at factory */
		if (priv->last_temperature > 100)
			temperature = priv->eeprom.groups[2].temperature;
		else /* else use most recent "sane" value from driver */
			temperature = priv->last_temperature;
	}

	return temperature;	/* raw, not "human readable" */
}

/* Adjust Txpower only if temperature variance is greater than threshold.
 *
 * Both are lower than older versions' 9 degrees */
#define IWL_TEMPERATURE_LIMIT_TIMER   6

/**
 * is_temp_calib_needed - determines if new calibration is needed
 *
 * records new temperature in tx_mgr->temperature.
 * replaces tx_mgr->last_temperature *only* if calib needed
 *    (assumes caller will actually do the calibration!). */
static int is_temp_calib_needed(struct iwl_priv *priv)
{
	int temp_diff;

	priv->temperature = iwl_hw_reg_txpower_get_temperature(priv);
	temp_diff = priv->temperature - priv->last_temperature;

	/* get absolute value */
	if (temp_diff < 0) {
		IWL_DEBUG_POWER("Getting cooler, delta %d,\n", temp_diff);
		temp_diff = -temp_diff;
	} else if (temp_diff == 0)
		IWL_DEBUG_POWER("Same temp,\n");
	else
		IWL_DEBUG_POWER("Getting warmer, delta %d,\n", temp_diff);

	/* if we don't need calibration, *don't* update last_temperature */
	if (temp_diff < IWL_TEMPERATURE_LIMIT_TIMER) {
		IWL_DEBUG_POWER("Timed thermal calib not needed\n");
		return 0;
	}

	IWL_DEBUG_POWER("Timed thermal calib needed\n");

	/* assume that caller will actually do calib ...
	 *   update the "last temperature" value */
	priv->last_temperature = priv->temperature;
	return 1;
}

#define IWL_MAX_GAIN_ENTRIES 78
#define IWL_CCK_FROM_OFDM_POWER_DIFF  -5
#define IWL_CCK_FROM_OFDM_INDEX_DIFF (10)

/* radio and DSP power table, each step is 1/2 dB.
 * 1st number is for RF analog gain, 2nd number is for DSP pre-DAC gain. */
static struct iwl_tx_power power_gain_table[2][IWL_MAX_GAIN_ENTRIES] = {
	{
	 {251, 127},		/* 2.4 GHz, highest power */
	 {251, 127},
	 {251, 127},
	 {251, 127},
	 {251, 125},
	 {251, 110},
	 {251, 105},
	 {251, 98},
	 {187, 125},
	 {187, 115},
	 {187, 108},
	 {187, 99},
	 {243, 119},
	 {243, 111},
	 {243, 105},
	 {243, 97},
	 {243, 92},
	 {211, 106},
	 {211, 100},
	 {179, 120},
	 {179, 113},
	 {179, 107},
	 {147, 125},
	 {147, 119},
	 {147, 112},
	 {147, 106},
	 {147, 101},
	 {147, 97},
	 {147, 91},
	 {115, 107},
	 {235, 121},
	 {235, 115},
	 {235, 109},
	 {203, 127},
	 {203, 121},
	 {203, 115},
	 {203, 108},
	 {203, 102},
	 {203, 96},
	 {203, 92},
	 {171, 110},
	 {171, 104},
	 {171, 98},
	 {139, 116},
	 {227, 125},
	 {227, 119},
	 {227, 113},
	 {227, 107},
	 {227, 101},
	 {227, 96},
	 {195, 113},
	 {195, 106},
	 {195, 102},
	 {195, 95},
	 {163, 113},
	 {163, 106},
	 {163, 102},
	 {163, 95},
	 {131, 113},
	 {131, 106},
	 {131, 102},
	 {131, 95},
	 {99, 113},
	 {99, 106},
	 {99, 102},
	 {99, 95},
	 {67, 113},
	 {67, 106},
	 {67, 102},
	 {67, 95},
	 {35, 113},
	 {35, 106},
	 {35, 102},
	 {35, 95},
	 {3, 113},
	 {3, 106},
	 {3, 102},
	 {3, 95} },		/* 2.4 GHz, lowest power */
	{
	 {251, 127},		/* 5.x GHz, highest power */
	 {251, 120},
	 {251, 114},
	 {219, 119},
	 {219, 101},
	 {187, 113},
	 {187, 102},
	 {155, 114},
	 {155, 103},
	 {123, 117},
	 {123, 107},
	 {123, 99},
	 {123, 92},
	 {91, 108},
	 {59, 125},
	 {59, 118},
	 {59, 109},
	 {59, 102},
	 {59, 96},
	 {59, 90},
	 {27, 104},
	 {27, 98},
	 {27, 92},
	 {115, 118},
	 {115, 111},
	 {115, 104},
	 {83, 126},
	 {83, 121},
	 {83, 113},
	 {83, 105},
	 {83, 99},
	 {51, 118},
	 {51, 111},
	 {51, 104},
	 {51, 98},
	 {19, 116},
	 {19, 109},
	 {19, 102},
	 {19, 98},
	 {19, 93},
	 {171, 113},
	 {171, 107},
	 {171, 99},
	 {139, 120},
	 {139, 113},
	 {139, 107},
	 {139, 99},
	 {107, 120},
	 {107, 113},
	 {107, 107},
	 {107, 99},
	 {75, 120},
	 {75, 113},
	 {75, 107},
	 {75, 99},
	 {43, 120},
	 {43, 113},
	 {43, 107},
	 {43, 99},
	 {11, 120},
	 {11, 113},
	 {11, 107},
	 {11, 99},
	 {131, 107},
	 {131, 99},
	 {99, 120},
	 {99, 113},
	 {99, 107},
	 {99, 99},
	 {67, 120},
	 {67, 113},
	 {67, 107},
	 {67, 99},
	 {35, 120},
	 {35, 113},
	 {35, 107},
	 {35, 99},
	 {3, 120} }		/* 5.x GHz, lowest power */
};

static inline u8 iwl_hw_reg_fix_power_index(int index)
{
	if (index < 0)
		return 0;
	if (index >= IWL_MAX_GAIN_ENTRIES)
		return IWL_MAX_GAIN_ENTRIES - 1;
	return (u8) index;
}

/* Kick off thermal recalibration check every 60 seconds */
#define REG_RECALIB_PERIOD (60)

/**
 * iwl_hw_reg_set_scan_power - Set Tx power for scan probe requests
 *
 * Set (in our channel info database) the direct scan Tx power for 1 Mbit (CCK)
 * or 6 Mbit (OFDM) rates.
 */
static void iwl_hw_reg_set_scan_power(struct iwl_priv *priv, u32 scan_tbl_index,
			       s32 rate_index, const s8 *clip_pwrs,
			       struct iwl_channel_info *ch_info,
			       int band_index)
{
	struct iwl_scan_power_info *scan_power_info;
	s8 power;
	u8 power_index;

	scan_power_info = &ch_info->scan_pwr_info[scan_tbl_index];

	/* use this channel group's 6Mbit clipping/saturation pwr,
	 *   but cap at regulatory scan power restriction (set during init
	 *   based on eeprom channel data) for this channel.  */
	power = min(ch_info->scan_power, clip_pwrs[IWL_RATE_6M_INDEX]);

	/* further limit to user's max power preference.
	 * FIXME:  Other spectrum management power limitations do not
	 *   seem to apply?? */
	power = min(power, priv->user_txpower_limit);
	scan_power_info->requested_power = power;

	/* find difference between new scan *power* and current "normal"
	 *   Tx *power* for 6Mb.  Use this difference (x2) to adjust the
	 *   current "normal" temperature-compensated Tx power *index* for
	 *   this rate (1Mb or 6Mb) to yield new temp-compensated scan power
	 *   *index*. */
	power_index = ch_info->power_info[rate_index].power_table_index
	    - (power - ch_info->power_info
	       [IWL_RATE_6M_INDEX].requested_power) * 2;

	/* store reference index that we use when adjusting *all* scan
	 *   powers.  So we can accommodate user (all channel) or spectrum
	 *   management (single channel) power changes "between" temperature
	 *   feedback compensation procedures.
	 * don't force fit this reference index into gain table; it may be a
	 *   negative number.  This will help avoid errors when we're at
	 *   the lower bounds (highest gains, for warmest temperatures)
	 *   of the table. */

	/* don't exceed table bounds for "real" setting */
	power_index = iwl_hw_reg_fix_power_index(power_index);

	scan_power_info->power_table_index = power_index;
	scan_power_info->tpc.tx_gain =
	    power_gain_table[band_index][power_index].tx_gain;
	scan_power_info->tpc.dsp_atten =
	    power_gain_table[band_index][power_index].dsp_atten;
}

/**
 * iwl_hw_reg_send_txpower - fill in Tx Power command with gain settings
 *
 * Configures power settings for all rates for the current channel,
 * using values from channel info struct, and send to NIC
 */
int iwl_hw_reg_send_txpower(struct iwl_priv *priv)
{
	int rate_idx;
	const struct iwl_channel_info *ch_info = NULL;
	struct iwl_txpowertable_cmd txpower = {
		.channel = priv->active_rxon.channel,
	};

	txpower.band = (priv->phymode == MODE_IEEE80211A) ? 0 : 1;
	ch_info = iwl_get_channel_info(priv,
				       priv->phymode,
				       le16_to_cpu(priv->active_rxon.channel));
	if (!ch_info) {
		IWL_ERROR
		    ("Failed to get channel info for channel %d [%d]\n",
		     le16_to_cpu(priv->active_rxon.channel), priv->phymode);
		return -EINVAL;
	}

	if (!is_channel_valid(ch_info)) {
		IWL_DEBUG_POWER("Not calling TX_PWR_TABLE_CMD on "
				"non-Tx channel.\n");
		return 0;
	}

	/* fill cmd with power settings for all rates for current channel */
	for (rate_idx = 0; rate_idx < IWL_RATE_COUNT; rate_idx++) {
		txpower.power[rate_idx].tpc = ch_info->power_info[rate_idx].tpc;
		txpower.power[rate_idx].rate = iwl_rates[rate_idx].plcp;

		IWL_DEBUG_POWER("ch %d:%d rf %d dsp %3d rate code 0x%02x\n",
				le16_to_cpu(txpower.channel),
				txpower.band,
				txpower.power[rate_idx].tpc.tx_gain,
				txpower.power[rate_idx].tpc.dsp_atten,
				txpower.power[rate_idx].rate);
	}

	return iwl_send_cmd_pdu(priv, REPLY_TX_PWR_TABLE_CMD,
				sizeof(struct iwl_txpowertable_cmd), &txpower);

}

/**
 * iwl_hw_reg_set_new_power - Configures power tables at new levels
 * @ch_info: Channel to update.  Uses power_info.requested_power.
 *
 * Replace requested_power and base_power_index ch_info fields for
 * one channel.
 *
 * Called if user or spectrum management changes power preferences.
 * Takes into account h/w and modulation limitations (clip power).
 *
 * This does *not* send anything to NIC, just sets up ch_info for one channel.
 *
 * NOTE: reg_compensate_for_temperature_dif() *must* be run after this to
 *	 properly fill out the scan powers, and actual h/w gain settings,
 *	 and send changes to NIC
 */
static int iwl_hw_reg_set_new_power(struct iwl_priv *priv,
			     struct iwl_channel_info *ch_info)
{
	struct iwl_channel_power_info *power_info;
	int power_changed = 0;
	int i;
	const s8 *clip_pwrs;
	int power;

	/* Get this chnlgrp's rate-to-max/clip-powers table */
	clip_pwrs = priv->clip_groups[ch_info->group_index].clip_powers;

	/* Get this channel's rate-to-current-power settings table */
	power_info = ch_info->power_info;

	/* update OFDM Txpower settings */
	for (i = IWL_FIRST_OFDM_RATE; i <= IWL_LAST_OFDM_RATE;
	     i++, ++power_info) {
		int delta_idx;

		/* limit new power to be no more than h/w capability */
		power = min(ch_info->curr_txpow, clip_pwrs[i]);
		if (power == power_info->requested_power)
			continue;

		/* find difference between old and new requested powers,
		 *    update base (non-temp-compensated) power index */
		delta_idx = (power - power_info->requested_power) * 2;
		power_info->base_power_index -= delta_idx;

		/* save new requested power value */
		power_info->requested_power = power;

		power_changed = 1;
	}

	/* update CCK Txpower settings, based on OFDM 12M setting ...
	 *    ... all CCK power settings for a given channel are the *same*. */
	if (power_changed) {
		power =
		    ch_info->power_info[IWL_RATE_12M_INDEX].
		    requested_power + IWL_CCK_FROM_OFDM_POWER_DIFF;

		/* do all CCK rates' iwl_channel_power_info structures */
		for (i = IWL_FIRST_CCK_RATE; i <= IWL_LAST_CCK_RATE; i++) {
			power_info->requested_power = power;
			power_info->base_power_index =
			    ch_info->power_info[IWL_RATE_12M_INDEX].
			    base_power_index + IWL_CCK_FROM_OFDM_INDEX_DIFF;
			++power_info;
		}
	}

	return 0;
}

/**
 * iwl_hw_reg_get_ch_txpower_limit - returns new power limit for channel
 *
 * NOTE: Returned power limit may be less (but not more) than requested,
 *	 based strictly on regulatory (eeprom and spectrum mgt) limitations
 *	 (no consideration for h/w clipping limitations).
 */
static int iwl_hw_reg_get_ch_txpower_limit(struct iwl_channel_info *ch_info)
{
	s8 max_power;

#if 0
	/* if we're using TGd limits, use lower of TGd or EEPROM */
	if (ch_info->tgd_data.max_power != 0)
		max_power = min(ch_info->tgd_data.max_power,
				ch_info->eeprom.max_power_avg);

	/* else just use EEPROM limits */
	else
#endif
		max_power = ch_info->eeprom.max_power_avg;

	return min(max_power, ch_info->max_power_avg);
}

/**
 * iwl_hw_reg_comp_txpower_temp - Compensate for temperature
 *
 * Compensate txpower settings of *all* channels for temperature.
 * This only accounts for the difference between current temperature
 *   and the factory calibration temperatures, and bases the new settings
 *   on the channel's base_power_index.
 *
 * If RxOn is "associated", this sends the new Txpower to NIC!
 */
static int iwl_hw_reg_comp_txpower_temp(struct iwl_priv *priv)
{
	struct iwl_channel_info *ch_info = NULL;
	int delta_index;
	const s8 *clip_pwrs; /* array of h/w max power levels for each rate */
	u8 a_band;
	u8 rate_index;
	u8 scan_tbl_index;
	u8 i;
	int ref_temp;
	int temperature = priv->temperature;

	/* set up new Tx power info for each and every channel, 2.4 and 5.x */
	for (i = 0; i < priv->channel_count; i++) {
		ch_info = &priv->channel_info[i];
		a_band = is_channel_a_band(ch_info);

		/* Get this chnlgrp's factory calibration temperature */
		ref_temp = (s16)priv->eeprom.groups[ch_info->group_index].
		    temperature;

		/* get power index adjustment based on curr and factory
		 * temps */
		delta_index = iwl_hw_reg_adjust_power_by_temp(temperature,
							      ref_temp);

		/* set tx power value for all rates, OFDM and CCK */
		for (rate_index = 0; rate_index < IWL_RATE_COUNT;
		     rate_index++) {
			int power_idx =
			    ch_info->power_info[rate_index].base_power_index;

			/* temperature compensate */
			power_idx += delta_index;

			/* stay within table range */
			power_idx = iwl_hw_reg_fix_power_index(power_idx);
			ch_info->power_info[rate_index].
			    power_table_index = (u8) power_idx;
			ch_info->power_info[rate_index].tpc =
			    power_gain_table[a_band][power_idx];
		}

		/* Get this chnlgrp's rate-to-max/clip-powers table */
		clip_pwrs = priv->clip_groups[ch_info->group_index].clip_powers;

		/* set scan tx power, 1Mbit for CCK, 6Mbit for OFDM */
		for (scan_tbl_index = 0;
		     scan_tbl_index < IWL_NUM_SCAN_RATES; scan_tbl_index++) {
			s32 actual_index = (scan_tbl_index == 0) ?
			    IWL_RATE_1M_INDEX : IWL_RATE_6M_INDEX;
			iwl_hw_reg_set_scan_power(priv, scan_tbl_index,
					   actual_index, clip_pwrs,
					   ch_info, a_band);
		}
	}

	/* send Txpower command for current channel to ucode */
	return iwl_hw_reg_send_txpower(priv);
}

int iwl_hw_reg_set_txpower(struct iwl_priv *priv, s8 power)
{
	struct iwl_channel_info *ch_info;
	s8 max_power;
	u8 a_band;
	u8 i;

	if (priv->user_txpower_limit == power) {
		IWL_DEBUG_POWER("Requested Tx power same as current "
				"limit: %ddBm.\n", power);
		return 0;
	}

	IWL_DEBUG_POWER("Setting upper limit clamp to %ddBm.\n", power);
	priv->user_txpower_limit = power;

	/* set up new Tx powers for each and every channel, 2.4 and 5.x */

	for (i = 0; i < priv->channel_count; i++) {
		ch_info = &priv->channel_info[i];
		a_band = is_channel_a_band(ch_info);

		/* find minimum power of all user and regulatory constraints
		 *    (does not consider h/w clipping limitations) */
		max_power = iwl_hw_reg_get_ch_txpower_limit(ch_info);
		max_power = min(power, max_power);
		if (max_power != ch_info->curr_txpow) {
			ch_info->curr_txpow = max_power;

			/* this considers the h/w clipping limitations */
			iwl_hw_reg_set_new_power(priv, ch_info);
		}
	}

	/* update txpower settings for all channels,
	 *   send to NIC if associated. */
	is_temp_calib_needed(priv);
	iwl_hw_reg_comp_txpower_temp(priv);

	return 0;
}

/* will add 3945 channel switch cmd handling later */
int iwl_hw_channel_switch(struct iwl_priv *priv, u16 channel)
{
	return 0;
}

/**
 * iwl3945_reg_txpower_periodic -  called when time to check our temperature.
 *
 * -- reset periodic timer
 * -- see if temp has changed enough to warrant re-calibration ... if so:
 *     -- correct coeffs for temp (can reset temp timer)
 *     -- save this temp as "last",
 *     -- send new set of gain settings to NIC
 * NOTE:  This should continue working, even when we're not associated,
 *   so we can keep our internal table of scan powers current. */
void iwl3945_reg_txpower_periodic(struct iwl_priv *priv)
{
	/* This will kick in the "brute force"
	 * iwl_hw_reg_comp_txpower_temp() below */
	if (!is_temp_calib_needed(priv))
		goto reschedule;

	/* Set up a new set of temp-adjusted TxPowers, send to NIC.
	 * This is based *only* on current temperature,
	 * ignoring any previous power measurements */
	iwl_hw_reg_comp_txpower_temp(priv);

 reschedule:
	queue_delayed_work(priv->workqueue,
			   &priv->thermal_periodic, REG_RECALIB_PERIOD * HZ);
}

void iwl3945_bg_reg_txpower_periodic(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv,
					     thermal_periodic.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwl3945_reg_txpower_periodic(priv);
	mutex_unlock(&priv->mutex);
}

/**
 * iwl_hw_reg_get_ch_grp_index - find the channel-group index (0-4)
 * 				   for the channel.
 *
 * This function is used when initializing channel-info structs.
 *
 * NOTE: These channel groups do *NOT* match the bands above!
 *	 These channel groups are based on factory-tested channels;
 *	 on A-band, EEPROM's "group frequency" entries represent the top
 *	 channel in each group 1-4.  Group 5 All B/G channels are in group 0.
 */
static u16 iwl_hw_reg_get_ch_grp_index(struct iwl_priv *priv,
				       const struct iwl_channel_info *ch_info)
{
	struct iwl_eeprom_txpower_group *ch_grp = &priv->eeprom.groups[0];
	u8 group;
	u16 group_index = 0;	/* based on factory calib frequencies */
	u8 grp_channel;

	/* Find the group index for the channel ... don't use index 1(?) */
	if (is_channel_a_band(ch_info)) {
		for (group = 1; group < 5; group++) {
			grp_channel = ch_grp[group].group_channel;
			if (ch_info->channel <= grp_channel) {
				group_index = group;
				break;
			}
		}
		/* group 4 has a few channels *above* its factory cal freq */
		if (group == 5)
			group_index = 4;
	} else
		group_index = 0;	/* 2.4 GHz, group 0 */

	IWL_DEBUG_POWER("Chnl %d mapped to grp %d\n", ch_info->channel,
			group_index);
	return group_index;
}

/**
 * iwl_hw_reg_get_matched_power_index - Interpolate to get nominal index
 *
 * Interpolate to get nominal (i.e. at factory calibration temperature) index
 *   into radio/DSP gain settings table for requested power.
 */
static int iwl_hw_reg_get_matched_power_index(struct iwl_priv *priv,
				       s8 requested_power,
				       s32 setting_index, s32 *new_index)
{
	const struct iwl_eeprom_txpower_group *chnl_grp = NULL;
	s32 index0, index1;
	s32 power = 2 * requested_power;
	s32 i;
	const struct iwl_eeprom_txpower_sample *samples;
	s32 gains0, gains1;
	s32 res;
	s32 denominator;

	chnl_grp = &priv->eeprom.groups[setting_index];
	samples = chnl_grp->samples;
	for (i = 0; i < 5; i++) {
		if (power == samples[i].power) {
			*new_index = samples[i].gain_index;
			return 0;
		}
	}

	if (power > samples[1].power) {
		index0 = 0;
		index1 = 1;
	} else if (power > samples[2].power) {
		index0 = 1;
		index1 = 2;
	} else if (power > samples[3].power) {
		index0 = 2;
		index1 = 3;
	} else {
		index0 = 3;
		index1 = 4;
	}

	denominator = (s32) samples[index1].power - (s32) samples[index0].power;
	if (denominator == 0)
		return -EINVAL;
	gains0 = (s32) samples[index0].gain_index * (1 << 19);
	gains1 = (s32) samples[index1].gain_index * (1 << 19);
	res = gains0 + (gains1 - gains0) *
	    ((s32) power - (s32) samples[index0].power) / denominator +
	    (1 << 18);
	*new_index = res >> 19;
	return 0;
}

static void iwl_hw_reg_init_channel_groups(struct iwl_priv *priv)
{
	u32 i;
	s32 rate_index;
	const struct iwl_eeprom_txpower_group *group;

	IWL_DEBUG_POWER("Initializing factory calib info from EEPROM\n");

	for (i = 0; i < IWL_NUM_TX_CALIB_GROUPS; i++) {
		s8 *clip_pwrs;	/* table of power levels for each rate */
		s8 satur_pwr;	/* saturation power for each chnl group */
		group = &priv->eeprom.groups[i];

		/* sanity check on factory saturation power value */
		if (group->saturation_power < 40) {
			IWL_WARNING("Error: saturation power is %d, "
				    "less than minimum expected 40\n",
				    group->saturation_power);
			return;
		}

		/*
		 * Derive requested power levels for each rate, based on
		 *   hardware capabilities (saturation power for band).
		 * Basic value is 3dB down from saturation, with further
		 *   power reductions for highest 3 data rates.  These
		 *   backoffs provide headroom for high rate modulation
		 *   power peaks, without too much distortion (clipping).
		 */
		/* we'll fill in this array with h/w max power levels */
		clip_pwrs = (s8 *) priv->clip_groups[i].clip_powers;

		/* divide factory saturation power by 2 to find -3dB level */
		satur_pwr = (s8) (group->saturation_power >> 1);

		/* fill in channel group's nominal powers for each rate */
		for (rate_index = 0;
		     rate_index < IWL_RATE_COUNT; rate_index++, clip_pwrs++) {
			switch (rate_index) {
			case IWL_RATE_36M_INDEX:
				if (i == 0)	/* B/G */
					*clip_pwrs = satur_pwr;
				else	/* A */
					*clip_pwrs = satur_pwr - 5;
				break;
			case IWL_RATE_48M_INDEX:
				if (i == 0)
					*clip_pwrs = satur_pwr - 7;
				else
					*clip_pwrs = satur_pwr - 10;
				break;
			case IWL_RATE_54M_INDEX:
				if (i == 0)
					*clip_pwrs = satur_pwr - 9;
				else
					*clip_pwrs = satur_pwr - 12;
				break;
			default:
				*clip_pwrs = satur_pwr;
				break;
			}
		}
	}
}

/**
 * iwl3945_txpower_set_from_eeprom - Set channel power info based on EEPROM
 *
 * Second pass (during init) to set up priv->channel_info
 *
 * Set up Tx-power settings in our channel info database for each VALID
 * (for this geo/SKU) channel, at all Tx data rates, based on eeprom values
 * and current temperature.
 *
 * Since this is based on current temperature (at init time), these values may
 * not be valid for very long, but it gives us a starting/default point,
 * and allows us to active (i.e. using Tx) scan.
 *
 * This does *not* write values to NIC, just sets up our internal table.
 */
int iwl3945_txpower_set_from_eeprom(struct iwl_priv *priv)
{
	struct iwl_channel_info *ch_info = NULL;
	struct iwl_channel_power_info *pwr_info;
	int delta_index;
	u8 rate_index;
	u8 scan_tbl_index;
	const s8 *clip_pwrs;	/* array of power levels for each rate */
	u8 gain, dsp_atten;
	s8 power;
	u8 pwr_index, base_pwr_index, a_band;
	u8 i;
	int temperature;

	/* save temperature reference,
	 *   so we can determine next time to calibrate */
	temperature = iwl_hw_reg_txpower_get_temperature(priv);
	priv->last_temperature = temperature;

	iwl_hw_reg_init_channel_groups(priv);

	/* initialize Tx power info for each and every channel, 2.4 and 5.x */
	for (i = 0, ch_info = priv->channel_info; i < priv->channel_count;
	     i++, ch_info++) {
		a_band = is_channel_a_band(ch_info);
		if (!is_channel_valid(ch_info))
			continue;

		/* find this channel's channel group (*not* "band") index */
		ch_info->group_index =
			iwl_hw_reg_get_ch_grp_index(priv, ch_info);

		/* Get this chnlgrp's rate->max/clip-powers table */
		clip_pwrs = priv->clip_groups[ch_info->group_index].clip_powers;

		/* calculate power index *adjustment* value according to
		 *  diff between current temperature and factory temperature */
		delta_index = iwl_hw_reg_adjust_power_by_temp(temperature,
				priv->eeprom.groups[ch_info->group_index].
				temperature);

		IWL_DEBUG_POWER("Delta index for channel %d: %d [%d]\n",
				ch_info->channel, delta_index, temperature +
				IWL_TEMP_CONVERT);

		/* set tx power value for all OFDM rates */
		for (rate_index = 0; rate_index < IWL_OFDM_RATES;
		     rate_index++) {
			s32 power_idx;
			int rc;

			/* use channel group's clip-power table,
			 *   but don't exceed channel's max power */
			s8 pwr = min(ch_info->max_power_avg,
				     clip_pwrs[rate_index]);

			pwr_info = &ch_info->power_info[rate_index];

			/* get base (i.e. at factory-measured temperature)
			 *    power table index for this rate's power */
			rc = iwl_hw_reg_get_matched_power_index(priv, pwr,
							 ch_info->group_index,
							 &power_idx);
			if (rc) {
				IWL_ERROR("Invalid power index\n");
				return rc;
			}
			pwr_info->base_power_index = (u8) power_idx;

			/* temperature compensate */
			power_idx += delta_index;

			/* stay within range of gain table */
			power_idx = iwl_hw_reg_fix_power_index(power_idx);

			/* fill 1 OFDM rate's iwl_channel_power_info struct */
			pwr_info->requested_power = pwr;
			pwr_info->power_table_index = (u8) power_idx;
			pwr_info->tpc.tx_gain =
			    power_gain_table[a_band][power_idx].tx_gain;
			pwr_info->tpc.dsp_atten =
			    power_gain_table[a_band][power_idx].dsp_atten;
		}

		/* set tx power for CCK rates, based on OFDM 12 Mbit settings*/
		pwr_info = &ch_info->power_info[IWL_RATE_12M_INDEX];
		power = pwr_info->requested_power +
			IWL_CCK_FROM_OFDM_POWER_DIFF;
		pwr_index = pwr_info->power_table_index +
			IWL_CCK_FROM_OFDM_INDEX_DIFF;
		base_pwr_index = pwr_info->base_power_index +
			IWL_CCK_FROM_OFDM_INDEX_DIFF;

		/* stay within table range */
		pwr_index = iwl_hw_reg_fix_power_index(pwr_index);
		gain = power_gain_table[a_band][pwr_index].tx_gain;
		dsp_atten = power_gain_table[a_band][pwr_index].dsp_atten;

		/* fill each CCK rate's iwl_channel_power_info structure
		 * NOTE:  All CCK-rate Txpwrs are the same for a given chnl!
		 * NOTE:  CCK rates start at end of OFDM rates! */
		for (rate_index = IWL_OFDM_RATES;
		     rate_index < IWL_RATE_COUNT; rate_index++) {
			pwr_info = &ch_info->power_info[rate_index];
			pwr_info->requested_power = power;
			pwr_info->power_table_index = pwr_index;
			pwr_info->base_power_index = base_pwr_index;
			pwr_info->tpc.tx_gain = gain;
			pwr_info->tpc.dsp_atten = dsp_atten;
		}

		/* set scan tx power, 1Mbit for CCK, 6Mbit for OFDM */
		for (scan_tbl_index = 0;
		     scan_tbl_index < IWL_NUM_SCAN_RATES; scan_tbl_index++) {
			s32 actual_index = (scan_tbl_index == 0) ?
				IWL_RATE_1M_INDEX : IWL_RATE_6M_INDEX;
			iwl_hw_reg_set_scan_power(priv, scan_tbl_index,
				actual_index, clip_pwrs, ch_info, a_band);
		}
	}

	return 0;
}

int iwl_hw_rxq_stop(struct iwl_priv *priv)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	rc = iwl_grab_restricted_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}

	iwl_write_restricted(priv, FH_RCSR_CONFIG(0), 0);
	rc = iwl_poll_restricted_bit(priv, FH_RSSR_STATUS, (1 << 24), 1000);
	if (rc < 0)
		IWL_ERROR("Can't stop Rx DMA.\n");

	iwl_release_restricted_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

int iwl_hw_tx_queue_init(struct iwl_priv *priv, struct iwl_tx_queue *txq)
{
	int rc;
	unsigned long flags;
	int txq_id = txq->q.id;

	struct iwl_shared *shared_data = priv->hw_setting.shared_virt;

	shared_data->tx_base_ptr[txq_id] = cpu_to_le32((u32)txq->q.dma_addr);

	spin_lock_irqsave(&priv->lock, flags);
	rc = iwl_grab_restricted_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}
	iwl_write_restricted(priv, FH_CBCC_CTRL(txq_id), 0);
	iwl_write_restricted(priv, FH_CBCC_BASE(txq_id), 0);

	iwl_write_restricted(priv, FH_TCSR_CONFIG(txq_id),
		ALM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_NOINT |
		ALM_FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_TXF |
		ALM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_IFTFD |
		ALM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE_VAL |
		ALM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE);
	iwl_release_restricted_access(priv);

	/* fake read to flush all prev. writes */
	iwl_read32(priv, FH_TSSR_CBB_BASE);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

int iwl_hw_get_rx_read(struct iwl_priv *priv)
{
	struct iwl_shared *shared_data = priv->hw_setting.shared_virt;

	return le32_to_cpu(shared_data->rx_read_ptr[0]);
}

/**
 * iwl3945_init_hw_rate_table - Initialize the hardware rate fallback table
 */
int iwl3945_init_hw_rate_table(struct iwl_priv *priv)
{
	int rc, i;
	struct iwl_rate_scaling_cmd rate_cmd = {
		.reserved = {0, 0, 0},
	};
	struct iwl_rate_scaling_info *table = rate_cmd.table;

	for (i = 0; i < ARRAY_SIZE(iwl_rates); i++) {
		table[i].rate_n_flags =
			iwl_hw_set_rate_n_flags(iwl_rates[i].plcp, 0);
		table[i].try_cnt = priv->retry_rate;
		table[i].next_rate_index = iwl_get_prev_ieee_rate(i);
	}

	switch (priv->phymode) {
	case MODE_IEEE80211A:
		IWL_DEBUG_RATE("Select A mode rate scale\n");
		/* If one of the following CCK rates is used,
		 * have it fall back to the 6M OFDM rate */
		for (i = IWL_FIRST_CCK_RATE; i <= IWL_LAST_CCK_RATE; i++)
			table[i].next_rate_index = IWL_FIRST_OFDM_RATE;

		/* Don't fall back to CCK rates */
		table[IWL_RATE_12M_INDEX].next_rate_index = IWL_RATE_9M_INDEX;

		/* Don't drop out of OFDM rates */
		table[IWL_FIRST_OFDM_RATE].next_rate_index =
		    IWL_FIRST_OFDM_RATE;
		break;

	case MODE_IEEE80211B:
		IWL_DEBUG_RATE("Select B mode rate scale\n");
		/* If an OFDM rate is used, have it fall back to the
		 * 1M CCK rates */
		for (i = IWL_FIRST_OFDM_RATE; i <= IWL_LAST_OFDM_RATE; i++)
			table[i].next_rate_index = IWL_FIRST_CCK_RATE;

		/* CCK shouldn't fall back to OFDM... */
		table[IWL_RATE_11M_INDEX].next_rate_index = IWL_RATE_5M_INDEX;
		break;

	default:
		IWL_DEBUG_RATE("Select G mode rate scale\n");
		break;
	}

	/* Update the rate scaling for control frame Tx */
	rate_cmd.table_id = 0;
	rc = iwl_send_cmd_pdu(priv, REPLY_RATE_SCALE, sizeof(rate_cmd),
			      &rate_cmd);
	if (rc)
		return rc;

	/* Update the rate scaling for data frame Tx */
	rate_cmd.table_id = 1;
	return iwl_send_cmd_pdu(priv, REPLY_RATE_SCALE, sizeof(rate_cmd),
				&rate_cmd);
}

int iwl_hw_set_hw_setting(struct iwl_priv *priv)
{
	memset((void *)&priv->hw_setting, 0,
	       sizeof(struct iwl_driver_hw_info));

	priv->hw_setting.shared_virt =
	    pci_alloc_consistent(priv->pci_dev,
				 sizeof(struct iwl_shared),
				 &priv->hw_setting.shared_phys);

	if (!priv->hw_setting.shared_virt) {
		IWL_ERROR("failed to allocate pci memory\n");
		mutex_unlock(&priv->mutex);
		return -ENOMEM;
	}

	priv->hw_setting.ac_queue_count = AC_NUM;
	priv->hw_setting.rx_buffer_size = IWL_RX_BUF_SIZE;
	priv->hw_setting.tx_cmd_len = sizeof(struct iwl_tx_cmd);
	priv->hw_setting.max_rxq_size = RX_QUEUE_SIZE;
	priv->hw_setting.max_rxq_log = RX_QUEUE_SIZE_LOG;
	priv->hw_setting.cck_flag = 0;
	priv->hw_setting.max_stations = IWL3945_STATION_COUNT;
	priv->hw_setting.bcast_sta_id = IWL3945_BROADCAST_ID;
	return 0;
}

unsigned int iwl_hw_get_beacon_cmd(struct iwl_priv *priv,
			  struct iwl_frame *frame, u8 rate)
{
	struct iwl_tx_beacon_cmd *tx_beacon_cmd;
	unsigned int frame_size;

	tx_beacon_cmd = (struct iwl_tx_beacon_cmd *)&frame->u;
	memset(tx_beacon_cmd, 0, sizeof(*tx_beacon_cmd));

	tx_beacon_cmd->tx.sta_id = IWL3945_BROADCAST_ID;
	tx_beacon_cmd->tx.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;

	frame_size = iwl_fill_beacon_frame(priv,
				tx_beacon_cmd->frame,
				BROADCAST_ADDR,
				sizeof(frame->u) - sizeof(*tx_beacon_cmd));

	BUG_ON(frame_size > MAX_MPDU_SIZE);
	tx_beacon_cmd->tx.len = cpu_to_le16((u16)frame_size);

	tx_beacon_cmd->tx.rate = rate;
	tx_beacon_cmd->tx.tx_flags = (TX_CMD_FLG_SEQ_CTL_MSK |
				      TX_CMD_FLG_TSF_MSK);

	/* supp_rates[0] == OFDM  */
	tx_beacon_cmd->tx.supp_rates[0] = IWL_OFDM_BASIC_RATES_MASK;

	/* supp_rates[1] == CCK
	 *
	 * NOTE:  IWL_*_RATES_MASK are not in the order that supp_rates
	 * expects so we have to shift them around.
	 *
	 * supp_rates expects:
	 * CCK rates are bit0..3
	 *
	 * However IWL_*_RATES_MASK has:
	 * CCK rates are bit8..11
	 */
	tx_beacon_cmd->tx.supp_rates[1] =
		(IWL_CCK_BASIC_RATES_MASK >> 8) & 0xF;

	return (sizeof(struct iwl_tx_beacon_cmd) + frame_size);
}

void iwl_hw_rx_handler_setup(struct iwl_priv *priv)
{
	priv->rx_handlers[REPLY_3945_RX] = iwl3945_rx_reply_rx;
}

void iwl_hw_setup_deferred_work(struct iwl_priv *priv)
{
	INIT_DELAYED_WORK(&priv->thermal_periodic,
			  iwl3945_bg_reg_txpower_periodic);
}

void iwl_hw_cancel_deferred_work(struct iwl_priv *priv)
{
	cancel_delayed_work(&priv->thermal_periodic);
}

struct pci_device_id iwl_hw_card_ids[] = {
	{0x8086, 0x4222, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x8086, 0x4227, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0}
};

inline int iwl_eeprom_aqcuire_semaphore(struct iwl_priv *priv)
{
	_iwl_clear_bit(priv, CSR_EEPROM_GP, CSR_EEPROM_GP_IF_OWNER_MSK);
	return 0;
}

MODULE_DEVICE_TABLE(pci, iwl_hw_card_ids);
