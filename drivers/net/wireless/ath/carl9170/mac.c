/*
 * Atheros CARL9170 driver
 *
 * MAC programming
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <asm/unaligned.h>

#include "carl9170.h"
#include "cmd.h"

int carl9170_set_dyn_sifs_ack(struct ar9170 *ar)
{
	u32 val;

	if (conf_is_ht40(&ar->hw->conf))
		val = 0x010a;
	else {
		if (ar->hw->conf.channel->band == IEEE80211_BAND_2GHZ)
			val = 0x105;
		else
			val = 0x104;
	}

	return carl9170_write_reg(ar, AR9170_MAC_REG_DYNAMIC_SIFS_ACK, val);
}

int carl9170_set_rts_cts_rate(struct ar9170 *ar)
{
	u32 rts_rate, cts_rate;

	if (conf_is_ht(&ar->hw->conf)) {
		/* 12 mbit OFDM */
		rts_rate = 0x1da;
		cts_rate = 0x10a;
	} else {
		if (ar->hw->conf.channel->band == IEEE80211_BAND_2GHZ) {
			/* 11 mbit CCK */
			rts_rate = 033;
			cts_rate = 003;
		} else {
			/* 6 mbit OFDM */
			rts_rate = 0x1bb;
			cts_rate = 0x10b;
		}
	}

	return carl9170_write_reg(ar, AR9170_MAC_REG_RTS_CTS_RATE,
				  rts_rate | (cts_rate) << 16);
}

int carl9170_set_slot_time(struct ar9170 *ar)
{
	struct ieee80211_vif *vif;
	u32 slottime = 20;

	rcu_read_lock();
	vif = carl9170_get_main_vif(ar);
	if (!vif) {
		rcu_read_unlock();
		return 0;
	}

	if ((ar->hw->conf.channel->band == IEEE80211_BAND_5GHZ) ||
	    vif->bss_conf.use_short_slot)
		slottime = 9;

	rcu_read_unlock();

	return carl9170_write_reg(ar, AR9170_MAC_REG_SLOT_TIME,
				  slottime << 10);
}

int carl9170_set_mac_rates(struct ar9170 *ar)
{
	struct ieee80211_vif *vif;
	u32 basic, mandatory;

	rcu_read_lock();
	vif = carl9170_get_main_vif(ar);

	if (!vif) {
		rcu_read_unlock();
		return 0;
	}

	basic = (vif->bss_conf.basic_rates & 0xf);
	basic |= (vif->bss_conf.basic_rates & 0xff0) << 4;
	rcu_read_unlock();

	if (ar->hw->conf.channel->band == IEEE80211_BAND_5GHZ)
		mandatory = 0xff00; /* OFDM 6/9/12/18/24/36/48/54 */
	else
		mandatory = 0xff0f; /* OFDM (6/9../54) + CCK (1/2/5.5/11) */

	carl9170_regwrite_begin(ar);
	carl9170_regwrite(AR9170_MAC_REG_BASIC_RATE, basic);
	carl9170_regwrite(AR9170_MAC_REG_MANDATORY_RATE, mandatory);
	carl9170_regwrite_finish();

	return carl9170_regwrite_result();
}

int carl9170_set_qos(struct ar9170 *ar)
{
	carl9170_regwrite_begin(ar);

	carl9170_regwrite(AR9170_MAC_REG_AC0_CW, ar->edcf[0].cw_min |
			  (ar->edcf[0].cw_max << 16));
	carl9170_regwrite(AR9170_MAC_REG_AC1_CW, ar->edcf[1].cw_min |
			  (ar->edcf[1].cw_max << 16));
	carl9170_regwrite(AR9170_MAC_REG_AC2_CW, ar->edcf[2].cw_min |
			  (ar->edcf[2].cw_max << 16));
	carl9170_regwrite(AR9170_MAC_REG_AC3_CW, ar->edcf[3].cw_min |
			  (ar->edcf[3].cw_max << 16));
	carl9170_regwrite(AR9170_MAC_REG_AC4_CW, ar->edcf[4].cw_min |
			  (ar->edcf[4].cw_max << 16));

	carl9170_regwrite(AR9170_MAC_REG_AC2_AC1_AC0_AIFS,
			  ((ar->edcf[0].aifs * 9 + 10)) |
			  ((ar->edcf[1].aifs * 9 + 10) << 12) |
			  ((ar->edcf[2].aifs * 9 + 10) << 24));
	carl9170_regwrite(AR9170_MAC_REG_AC4_AC3_AC2_AIFS,
			  ((ar->edcf[2].aifs * 9 + 10) >> 8) |
			  ((ar->edcf[3].aifs * 9 + 10) << 4) |
			  ((ar->edcf[4].aifs * 9 + 10) << 16));

	carl9170_regwrite(AR9170_MAC_REG_AC1_AC0_TXOP,
			  ar->edcf[0].txop | ar->edcf[1].txop << 16);
	carl9170_regwrite(AR9170_MAC_REG_AC3_AC2_TXOP,
			  ar->edcf[2].txop | ar->edcf[3].txop << 16 |
			  ar->edcf[4].txop << 24);

	carl9170_regwrite_finish();

	return carl9170_regwrite_result();
}

int carl9170_init_mac(struct ar9170 *ar)
{
	carl9170_regwrite_begin(ar);

	/* switch MAC to OTUS interface */
	carl9170_regwrite(0x1c3600, 0x3);

	carl9170_regwrite(AR9170_MAC_REG_ACK_EXTENSION, 0x40);

	carl9170_regwrite(AR9170_MAC_REG_RETRY_MAX, 0x0);

	carl9170_regwrite(AR9170_MAC_REG_FRAMETYPE_FILTER,
			  AR9170_MAC_FTF_MONITOR);

	/* enable MMIC */
	carl9170_regwrite(AR9170_MAC_REG_SNIFFER,
			AR9170_MAC_SNIFFER_DEFAULTS);

	carl9170_regwrite(AR9170_MAC_REG_RX_THRESHOLD, 0xc1f80);

	carl9170_regwrite(AR9170_MAC_REG_RX_PE_DELAY, 0x70);
	carl9170_regwrite(AR9170_MAC_REG_EIFS_AND_SIFS, 0xa144000);
	carl9170_regwrite(AR9170_MAC_REG_SLOT_TIME, 9 << 10);

	/* CF-END & CF-ACK rate => 24M OFDM */
	carl9170_regwrite(AR9170_MAC_REG_TID_CFACK_CFEND_RATE, 0x59900000);

	/* NAV protects ACK only (in TXOP) */
	carl9170_regwrite(AR9170_MAC_REG_TXOP_DURATION, 0x201);

	/* Set Beacon PHY CTRL's TPC to 0x7, TA1=1 */
	/* OTUS set AM to 0x1 */
	carl9170_regwrite(AR9170_MAC_REG_BCN_HT1, 0x8000170);

	carl9170_regwrite(AR9170_MAC_REG_BACKOFF_PROTECT, 0x105);

	/* Aggregation MAX number and timeout */
	carl9170_regwrite(AR9170_MAC_REG_AMPDU_FACTOR, 0x8000a);
	carl9170_regwrite(AR9170_MAC_REG_AMPDU_DENSITY, 0x140a07);

	carl9170_regwrite(AR9170_MAC_REG_FRAMETYPE_FILTER,
			  AR9170_MAC_FTF_DEFAULTS);

	carl9170_regwrite(AR9170_MAC_REG_RX_CONTROL,
			  AR9170_MAC_RX_CTRL_DEAGG |
			  AR9170_MAC_RX_CTRL_SHORT_FILTER);

	/* rate sets */
	carl9170_regwrite(AR9170_MAC_REG_BASIC_RATE, 0x150f);
	carl9170_regwrite(AR9170_MAC_REG_MANDATORY_RATE, 0x150f);
	carl9170_regwrite(AR9170_MAC_REG_RTS_CTS_RATE, 0x0030033);

	/* MIMO response control */
	carl9170_regwrite(AR9170_MAC_REG_ACK_TPC, 0x4003c1e);

	carl9170_regwrite(AR9170_MAC_REG_AMPDU_RX_THRESH, 0xffff);

	/* set PHY register read timeout (??) */
	carl9170_regwrite(AR9170_MAC_REG_MISC_680, 0xf00008);

	/* Disable Rx TimeOut, workaround for BB. */
	carl9170_regwrite(AR9170_MAC_REG_RX_TIMEOUT, 0x0);

	/* Set WLAN DMA interrupt mode: generate int per packet */
	carl9170_regwrite(AR9170_MAC_REG_TXRX_MPI, 0x110011);

	carl9170_regwrite(AR9170_MAC_REG_FCS_SELECT,
			AR9170_MAC_FCS_FIFO_PROT);

	/* Disables the CF_END frame, undocumented register */
	carl9170_regwrite(AR9170_MAC_REG_TXOP_NOT_ENOUGH_IND,
			0x141e0f48);

	/* reset group hash table */
	carl9170_regwrite(AR9170_MAC_REG_GROUP_HASH_TBL_L, 0xffffffff);
	carl9170_regwrite(AR9170_MAC_REG_GROUP_HASH_TBL_H, 0xffffffff);

	/* disable PRETBTT interrupt */
	carl9170_regwrite(AR9170_MAC_REG_PRETBTT, 0x0);
	carl9170_regwrite(AR9170_MAC_REG_BCN_PERIOD, 0x0);

	carl9170_regwrite_finish();

	return carl9170_regwrite_result();
}

static int carl9170_set_mac_reg(struct ar9170 *ar,
				const u32 reg, const u8 *mac)
{
	static const u8 zero[ETH_ALEN] = { 0 };

	if (!mac)
		mac = zero;

	carl9170_regwrite_begin(ar);

	carl9170_regwrite(reg, get_unaligned_le32(mac));
	carl9170_regwrite(reg + 4, get_unaligned_le16(mac + 4));

	carl9170_regwrite_finish();

	return carl9170_regwrite_result();
}

int carl9170_mod_virtual_mac(struct ar9170 *ar, const unsigned int id,
			     const u8 *mac)
{
	if (WARN_ON(id >= ar->fw.vif_num))
		return -EINVAL;

	return carl9170_set_mac_reg(ar,
		AR9170_MAC_REG_ACK_TABLE + (id - 1) * 8, mac);
}

int carl9170_update_multicast(struct ar9170 *ar, const u64 mc_hash)
{
	int err;

	carl9170_regwrite_begin(ar);
	carl9170_regwrite(AR9170_MAC_REG_GROUP_HASH_TBL_H, mc_hash >> 32);
	carl9170_regwrite(AR9170_MAC_REG_GROUP_HASH_TBL_L, mc_hash);
	carl9170_regwrite_finish();
	err = carl9170_regwrite_result();
	if (err)
		return err;

	ar->cur_mc_hash = mc_hash;
	return 0;
}

int carl9170_set_operating_mode(struct ar9170 *ar)
{
	struct ieee80211_vif *vif;
	struct ath_common *common = &ar->common;
	u8 *mac_addr, *bssid;
	u32 cam_mode = AR9170_MAC_CAM_DEFAULTS;
	u32 enc_mode = AR9170_MAC_ENCRYPTION_DEFAULTS |
		AR9170_MAC_ENCRYPTION_MGMT_RX_SOFTWARE;
	u32 rx_ctrl = AR9170_MAC_RX_CTRL_DEAGG |
		      AR9170_MAC_RX_CTRL_SHORT_FILTER;
	u32 sniffer = AR9170_MAC_SNIFFER_DEFAULTS;
	int err = 0;

	rcu_read_lock();
	vif = carl9170_get_main_vif(ar);

	if (vif) {
		mac_addr = common->macaddr;
		bssid = common->curbssid;

		switch (vif->type) {
		case NL80211_IFTYPE_ADHOC:
			cam_mode |= AR9170_MAC_CAM_IBSS;
			break;
		case NL80211_IFTYPE_MESH_POINT:
		case NL80211_IFTYPE_AP:
			cam_mode |= AR9170_MAC_CAM_AP;

			/* iwlagn 802.11n STA Workaround */
			rx_ctrl |= AR9170_MAC_RX_CTRL_PASS_TO_HOST;
			break;
		case NL80211_IFTYPE_WDS:
			cam_mode |= AR9170_MAC_CAM_AP_WDS;
			rx_ctrl |= AR9170_MAC_RX_CTRL_PASS_TO_HOST;
			break;
		case NL80211_IFTYPE_STATION:
			cam_mode |= AR9170_MAC_CAM_STA;
			rx_ctrl |= AR9170_MAC_RX_CTRL_PASS_TO_HOST;
			break;
		default:
			WARN(1, "Unsupported operation mode %x\n", vif->type);
			err = -EOPNOTSUPP;
			break;
		}
	} else {
		mac_addr = NULL;
		bssid = NULL;
	}
	rcu_read_unlock();

	if (err)
		return err;

	if (ar->rx_software_decryption)
		enc_mode |= AR9170_MAC_ENCRYPTION_RX_SOFTWARE;

	if (ar->sniffer_enabled) {
		rx_ctrl |= AR9170_MAC_RX_CTRL_ACK_IN_SNIFFER;
		sniffer |= AR9170_MAC_SNIFFER_ENABLE_PROMISC;
		enc_mode |= AR9170_MAC_ENCRYPTION_RX_SOFTWARE;
	}

	err = carl9170_set_mac_reg(ar, AR9170_MAC_REG_MAC_ADDR_L, mac_addr);
	if (err)
		return err;

	err = carl9170_set_mac_reg(ar, AR9170_MAC_REG_BSSID_L, bssid);
	if (err)
		return err;

	carl9170_regwrite_begin(ar);
	carl9170_regwrite(AR9170_MAC_REG_SNIFFER, sniffer);
	carl9170_regwrite(AR9170_MAC_REG_CAM_MODE, cam_mode);
	carl9170_regwrite(AR9170_MAC_REG_ENCRYPTION, enc_mode);
	carl9170_regwrite(AR9170_MAC_REG_RX_CONTROL, rx_ctrl);
	carl9170_regwrite_finish();

	return carl9170_regwrite_result();
}

int carl9170_set_hwretry_limit(struct ar9170 *ar, const unsigned int max_retry)
{
	u32 tmp = min_t(u32, 0x33333, max_retry * 0x11111);

	return carl9170_write_reg(ar, AR9170_MAC_REG_RETRY_MAX, tmp);
}

int carl9170_set_beacon_timers(struct ar9170 *ar)
{
	struct ieee80211_vif *vif;
	u32 v = 0;
	u32 pretbtt = 0;

	rcu_read_lock();
	vif = carl9170_get_main_vif(ar);

	if (vif) {
		struct carl9170_vif_info *mvif;
		mvif = (void *) vif->drv_priv;

		if (mvif->enable_beacon && !WARN_ON(!ar->beacon_enabled)) {
			ar->global_beacon_int = vif->bss_conf.beacon_int /
						ar->beacon_enabled;

			SET_VAL(AR9170_MAC_BCN_DTIM, v,
				vif->bss_conf.dtim_period);

			switch (vif->type) {
			case NL80211_IFTYPE_MESH_POINT:
			case NL80211_IFTYPE_ADHOC:
				v |= AR9170_MAC_BCN_IBSS_MODE;
				break;
			case NL80211_IFTYPE_AP:
				v |= AR9170_MAC_BCN_AP_MODE;
				break;
			default:
				WARN_ON_ONCE(1);
				break;
			}
		} else if (vif->type == NL80211_IFTYPE_STATION) {
			ar->global_beacon_int = vif->bss_conf.beacon_int;

			SET_VAL(AR9170_MAC_BCN_DTIM, v,
				ar->hw->conf.ps_dtim_period);

			v |= AR9170_MAC_BCN_STA_PS |
			     AR9170_MAC_BCN_PWR_MGT;
		}

		if (ar->global_beacon_int) {
			if (ar->global_beacon_int < 15) {
				rcu_read_unlock();
				return -ERANGE;
			}

			ar->global_pretbtt = ar->global_beacon_int -
					CARL9170_PRETBTT_KUS;
		} else {
			ar->global_pretbtt = 0;
		}
	} else {
		ar->global_beacon_int = 0;
		ar->global_pretbtt = 0;
	}

	rcu_read_unlock();

	SET_VAL(AR9170_MAC_BCN_PERIOD, v, ar->global_beacon_int);
	SET_VAL(AR9170_MAC_PRETBTT, pretbtt, ar->global_pretbtt);
	SET_VAL(AR9170_MAC_PRETBTT2, pretbtt, ar->global_pretbtt);

	carl9170_regwrite_begin(ar);
	carl9170_regwrite(AR9170_MAC_REG_PRETBTT, pretbtt);
	carl9170_regwrite(AR9170_MAC_REG_BCN_PERIOD, v);
	carl9170_regwrite_finish();
	return carl9170_regwrite_result();
}

int carl9170_upload_key(struct ar9170 *ar, const u8 id, const u8 *mac,
			const u8 ktype, const u8 keyidx, const u8 *keydata,
			const int keylen)
{
	struct carl9170_set_key_cmd key = { };
	static const u8 bcast[ETH_ALEN] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	mac = mac ? : bcast;

	key.user = cpu_to_le16(id);
	key.keyId = cpu_to_le16(keyidx);
	key.type = cpu_to_le16(ktype);
	memcpy(&key.macAddr, mac, ETH_ALEN);
	if (keydata)
		memcpy(&key.key, keydata, keylen);

	return carl9170_exec_cmd(ar, CARL9170_CMD_EKEY,
		sizeof(key), (u8 *)&key, 0, NULL);
}

int carl9170_disable_key(struct ar9170 *ar, const u8 id)
{
	struct carl9170_disable_key_cmd key = { };

	key.user = cpu_to_le16(id);

	return carl9170_exec_cmd(ar, CARL9170_CMD_DKEY,
		sizeof(key), (u8 *)&key, 0, NULL);
}

int carl9170_set_mac_tpc(struct ar9170 *ar, struct ieee80211_channel *channel)
{
	unsigned int power, chains;

	if (ar->eeprom.tx_mask != 1)
		chains = AR9170_TX_PHY_TXCHAIN_2;
	else
		chains = AR9170_TX_PHY_TXCHAIN_1;

	switch (channel->band) {
	case IEEE80211_BAND_2GHZ:
		power = ar->power_2G_ofdm[0] & 0x3f;
		break;
	case IEEE80211_BAND_5GHZ:
		power = ar->power_5G_leg[0] & 0x3f;
		break;
	default:
		BUG_ON(1);
	}

	power = min_t(unsigned int, power, ar->hw->conf.power_level * 2);

	carl9170_regwrite_begin(ar);
	carl9170_regwrite(AR9170_MAC_REG_ACK_TPC,
			  0x3c1e | power << 20 | chains << 26);
	carl9170_regwrite(AR9170_MAC_REG_RTS_CTS_TPC,
			  power << 5 | chains << 11 |
			  power << 21 | chains << 27);
	carl9170_regwrite(AR9170_MAC_REG_CFEND_QOSNULL_TPC,
			  power << 5 | chains << 11 |
			  power << 21 | chains << 27);
	carl9170_regwrite_finish();
	return carl9170_regwrite_result();
}
