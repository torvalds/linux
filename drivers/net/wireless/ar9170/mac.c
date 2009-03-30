/*
 * Atheros AR9170 driver
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
#include "ar9170.h"
#include "cmd.h"

int ar9170_set_qos(struct ar9170 *ar)
{
	ar9170_regwrite_begin(ar);

	ar9170_regwrite(AR9170_MAC_REG_AC0_CW, ar->edcf[0].cw_min |
			(ar->edcf[0].cw_max << 16));
	ar9170_regwrite(AR9170_MAC_REG_AC1_CW, ar->edcf[1].cw_min |
			(ar->edcf[1].cw_max << 16));
	ar9170_regwrite(AR9170_MAC_REG_AC2_CW, ar->edcf[2].cw_min |
			(ar->edcf[2].cw_max << 16));
	ar9170_regwrite(AR9170_MAC_REG_AC3_CW, ar->edcf[3].cw_min |
			(ar->edcf[3].cw_max << 16));
	ar9170_regwrite(AR9170_MAC_REG_AC4_CW, ar->edcf[4].cw_min |
			(ar->edcf[4].cw_max << 16));

	ar9170_regwrite(AR9170_MAC_REG_AC1_AC0_AIFS,
			((ar->edcf[0].aifs * 9 + 10)) |
			((ar->edcf[1].aifs * 9 + 10) << 12) |
			((ar->edcf[2].aifs * 9 + 10) << 24));
	ar9170_regwrite(AR9170_MAC_REG_AC3_AC2_AIFS,
			((ar->edcf[2].aifs * 9 + 10) >> 8) |
			((ar->edcf[3].aifs * 9 + 10) << 4) |
			((ar->edcf[4].aifs * 9 + 10) << 16));

	ar9170_regwrite(AR9170_MAC_REG_AC1_AC0_TXOP,
			ar->edcf[0].txop | ar->edcf[1].txop << 16);
	ar9170_regwrite(AR9170_MAC_REG_AC3_AC2_TXOP,
			ar->edcf[1].txop | ar->edcf[3].txop << 16);

	ar9170_regwrite_finish();

	return ar9170_regwrite_result();
}

int ar9170_init_mac(struct ar9170 *ar)
{
	ar9170_regwrite_begin(ar);

	ar9170_regwrite(AR9170_MAC_REG_ACK_EXTENSION, 0x40);

	ar9170_regwrite(AR9170_MAC_REG_RETRY_MAX, 0);

	/* enable MMIC */
	ar9170_regwrite(AR9170_MAC_REG_SNIFFER,
			AR9170_MAC_REG_SNIFFER_DEFAULTS);

	ar9170_regwrite(AR9170_MAC_REG_RX_THRESHOLD, 0xc1f80);

	ar9170_regwrite(AR9170_MAC_REG_RX_PE_DELAY, 0x70);
	ar9170_regwrite(AR9170_MAC_REG_EIFS_AND_SIFS, 0xa144000);
	ar9170_regwrite(AR9170_MAC_REG_SLOT_TIME, 9 << 10);

	/* CF-END mode */
	ar9170_regwrite(0x1c3b2c, 0x19000000);

	/* NAV protects ACK only (in TXOP) */
	ar9170_regwrite(0x1c3b38, 0x201);

	/* Set Beacon PHY CTRL's TPC to 0x7, TA1=1 */
	/* OTUS set AM to 0x1 */
	ar9170_regwrite(AR9170_MAC_REG_BCN_HT1, 0x8000170);

	ar9170_regwrite(AR9170_MAC_REG_BACKOFF_PROTECT, 0x105);

	/* AGG test code*/
	/* Aggregation MAX number and timeout */
	ar9170_regwrite(0x1c3b9c, 0x10000a);

	ar9170_regwrite(AR9170_MAC_REG_FRAMETYPE_FILTER,
			AR9170_MAC_REG_FTF_DEFAULTS);

	/* Enable deaggregator, response in sniffer mode */
	ar9170_regwrite(0x1c3c40, 0x1 | 1<<30);

	/* rate sets */
	ar9170_regwrite(AR9170_MAC_REG_BASIC_RATE, 0x150f);
	ar9170_regwrite(AR9170_MAC_REG_MANDATORY_RATE, 0x150f);
	ar9170_regwrite(AR9170_MAC_REG_RTS_CTS_RATE, 0x10b01bb);

	/* MIMO response control */
	ar9170_regwrite(0x1c3694, 0x4003C1E);/* bit 26~28  otus-AM */

	/* switch MAC to OTUS interface */
	ar9170_regwrite(0x1c3600, 0x3);

	ar9170_regwrite(AR9170_MAC_REG_AMPDU_RX_THRESH, 0xffff);

	/* set PHY register read timeout (??) */
	ar9170_regwrite(AR9170_MAC_REG_MISC_680, 0xf00008);

	/* Disable Rx TimeOut, workaround for BB. */
	ar9170_regwrite(AR9170_MAC_REG_RX_TIMEOUT, 0x0);

	/* Set CPU clock frequency to 88/80MHz */
	ar9170_regwrite(AR9170_PWR_REG_CLOCK_SEL,
			AR9170_PWR_CLK_AHB_80_88MHZ |
			AR9170_PWR_CLK_DAC_160_INV_DLY);

	/* Set WLAN DMA interrupt mode: generate int per packet */
	ar9170_regwrite(AR9170_MAC_REG_TXRX_MPI, 0x110011);

	ar9170_regwrite(AR9170_MAC_REG_FCS_SELECT,
			AR9170_MAC_FCS_FIFO_PROT);

	/* Disables the CF_END frame, undocumented register */
	ar9170_regwrite(AR9170_MAC_REG_TXOP_NOT_ENOUGH_IND,
			0x141E0F48);

	ar9170_regwrite_finish();

	return ar9170_regwrite_result();
}

static int ar9170_set_mac_reg(struct ar9170 *ar, const u32 reg, const u8 *mac)
{
	static const u8 zero[ETH_ALEN] = { 0 };

	if (!mac)
		mac = zero;

	ar9170_regwrite_begin(ar);

	ar9170_regwrite(reg,
			(mac[3] << 24) | (mac[2] << 16) |
			(mac[1] << 8) | mac[0]);

	ar9170_regwrite(reg + 4, (mac[5] << 8) | mac[4]);

	ar9170_regwrite_finish();

	return ar9170_regwrite_result();
}

int ar9170_update_multicast(struct ar9170 *ar)
{
	int err;

	ar9170_regwrite_begin(ar);
	ar9170_regwrite(AR9170_MAC_REG_GROUP_HASH_TBL_H,
		ar->want_mc_hash >> 32);
	ar9170_regwrite(AR9170_MAC_REG_GROUP_HASH_TBL_L,
		ar->want_mc_hash);

	ar9170_regwrite_finish();
	err = ar9170_regwrite_result();

	if (err)
		return err;

	ar->cur_mc_hash = ar->want_mc_hash;

	return 0;
}

int ar9170_update_frame_filter(struct ar9170 *ar)
{
	int err;

	err = ar9170_write_reg(ar, AR9170_MAC_REG_FRAMETYPE_FILTER,
			       ar->want_filter);

	if (err)
		return err;

	ar->cur_filter = ar->want_filter;

	return 0;
}

static int ar9170_set_promiscouous(struct ar9170 *ar)
{
	u32 encr_mode, sniffer;
	int err;

	err = ar9170_read_reg(ar, AR9170_MAC_REG_SNIFFER, &sniffer);
	if (err)
		return err;

	err = ar9170_read_reg(ar, AR9170_MAC_REG_ENCRYPTION, &encr_mode);
	if (err)
		return err;

	if (ar->sniffer_enabled) {
		sniffer |= AR9170_MAC_REG_SNIFFER_ENABLE_PROMISC;

		/*
		 * Rx decryption works in place.
		 *
		 * If we don't disable it, the hardware will render all
		 * encrypted frames which are encrypted with an unknown
		 * key useless.
		 */

		encr_mode |= AR9170_MAC_REG_ENCRYPTION_RX_SOFTWARE;
		ar->sniffer_enabled = true;
	} else {
		sniffer &= ~AR9170_MAC_REG_SNIFFER_ENABLE_PROMISC;

		if (ar->rx_software_decryption)
			encr_mode |= AR9170_MAC_REG_ENCRYPTION_RX_SOFTWARE;
		else
			encr_mode &= ~AR9170_MAC_REG_ENCRYPTION_RX_SOFTWARE;
	}

	ar9170_regwrite_begin(ar);
	ar9170_regwrite(AR9170_MAC_REG_ENCRYPTION, encr_mode);
	ar9170_regwrite(AR9170_MAC_REG_SNIFFER, sniffer);
	ar9170_regwrite_finish();

	return ar9170_regwrite_result();
}

int ar9170_set_operating_mode(struct ar9170 *ar)
{
	u32 pm_mode = AR9170_MAC_REG_POWERMGT_DEFAULTS;
	u8 *mac_addr, *bssid;
	int err;

	if (ar->vif) {
		mac_addr = ar->mac_addr;
		bssid = ar->bssid;

		switch (ar->vif->type) {
		case NL80211_IFTYPE_MESH_POINT:
		case NL80211_IFTYPE_ADHOC:
			pm_mode |= AR9170_MAC_REG_POWERMGT_IBSS;
			break;
/*		case NL80211_IFTYPE_AP:
			pm_mode |= AR9170_MAC_REG_POWERMGT_AP;
			break;*/
		case NL80211_IFTYPE_WDS:
			pm_mode |= AR9170_MAC_REG_POWERMGT_AP_WDS;
			break;
		case NL80211_IFTYPE_MONITOR:
			ar->sniffer_enabled = true;
			ar->rx_software_decryption = true;
			break;
		default:
			pm_mode |= AR9170_MAC_REG_POWERMGT_STA;
			break;
		}
	} else {
		mac_addr = NULL;
		bssid = NULL;
	}

	err = ar9170_set_mac_reg(ar, AR9170_MAC_REG_MAC_ADDR_L, mac_addr);
	if (err)
		return err;

	err = ar9170_set_mac_reg(ar, AR9170_MAC_REG_BSSID_L, bssid);
	if (err)
		return err;

	err = ar9170_set_promiscouous(ar);
	if (err)
		return err;

	ar9170_regwrite_begin(ar);

	ar9170_regwrite(AR9170_MAC_REG_POWERMANAGEMENT, pm_mode);
	ar9170_regwrite_finish();

	return ar9170_regwrite_result();
}

int ar9170_set_hwretry_limit(struct ar9170 *ar, unsigned int max_retry)
{
	u32 tmp = min_t(u32, 0x33333, max_retry * 0x11111);

	return ar9170_write_reg(ar, AR9170_MAC_REG_RETRY_MAX, tmp);
}

int ar9170_set_beacon_timers(struct ar9170 *ar)
{
	u32 v = 0;
	u32 pretbtt = 0;

	v |= ar->hw->conf.beacon_int;

	if (ar->vif) {
		switch (ar->vif->type) {
		case NL80211_IFTYPE_MESH_POINT:
		case NL80211_IFTYPE_ADHOC:
			v |= BIT(25);
			break;
		case NL80211_IFTYPE_AP:
			v |= BIT(24);
			pretbtt = (ar->hw->conf.beacon_int - 6) << 16;
			break;
		default:
			break;
		}

		v |= ar->vif->bss_conf.dtim_period << 16;
	}

	ar9170_regwrite_begin(ar);

	ar9170_regwrite(AR9170_MAC_REG_PRETBTT, pretbtt);
	ar9170_regwrite(AR9170_MAC_REG_BCN_PERIOD, v);
	ar9170_regwrite_finish();
	return ar9170_regwrite_result();
}

int ar9170_update_beacon(struct ar9170 *ar)
{
	struct sk_buff *skb;
	__le32 *data, *old = NULL;
	u32 word;
	int i;

	skb = ieee80211_beacon_get(ar->hw, ar->vif);
	if (!skb)
		return -ENOMEM;

	data = (__le32 *)skb->data;
	if (ar->beacon)
		old = (__le32 *)ar->beacon->data;

	ar9170_regwrite_begin(ar);
	for (i = 0; i < DIV_ROUND_UP(skb->len, 4); i++) {
		/*
		 * XXX: This accesses beyond skb data for up
		 *	to the last 3 bytes!!
		 */

		if (old && (data[i] == old[i]))
			continue;

		word = le32_to_cpu(data[i]);
		ar9170_regwrite(AR9170_BEACON_BUFFER_ADDRESS + 4 * i, word);
	}

	/* XXX: use skb->cb info */
	if (ar->hw->conf.channel->band == IEEE80211_BAND_2GHZ)
		ar9170_regwrite(AR9170_MAC_REG_BCN_PLCP,
				((skb->len + 4) << (3+16)) + 0x0400);
	else
		ar9170_regwrite(AR9170_MAC_REG_BCN_PLCP,
				((skb->len + 4) << (3+16)) + 0x0400);

	ar9170_regwrite(AR9170_MAC_REG_BCN_LENGTH, skb->len + 4);
	ar9170_regwrite(AR9170_MAC_REG_BCN_ADDR, AR9170_BEACON_BUFFER_ADDRESS);
	ar9170_regwrite(AR9170_MAC_REG_BCN_CTRL, 1);

	ar9170_regwrite_finish();

	dev_kfree_skb(ar->beacon);
	ar->beacon = skb;

	return ar9170_regwrite_result();
}

void ar9170_new_beacon(struct work_struct *work)
{
	struct ar9170 *ar = container_of(work, struct ar9170,
					 beacon_work);
	struct sk_buff *skb;

	if (unlikely(!IS_STARTED(ar)))
		return ;

	mutex_lock(&ar->mutex);

	if (!ar->vif)
		goto out;

	ar9170_update_beacon(ar);

	rcu_read_lock();
	while ((skb = ieee80211_get_buffered_bc(ar->hw, ar->vif)))
		ar9170_op_tx(ar->hw, skb);

	rcu_read_unlock();

 out:
	mutex_unlock(&ar->mutex);
}

int ar9170_upload_key(struct ar9170 *ar, u8 id, const u8 *mac, u8 ktype,
		      u8 keyidx, u8 *keydata, int keylen)
{
	__le32 vals[7];
	static const u8 bcast[ETH_ALEN] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	u8 dummy;

	mac = mac ? : bcast;

	vals[0] = cpu_to_le32((keyidx << 16) + id);
	vals[1] = cpu_to_le32(mac[1] << 24 | mac[0] << 16 | ktype);
	vals[2] = cpu_to_le32(mac[5] << 24 | mac[4] << 16 |
			      mac[3] << 8 | mac[2]);
	memset(&vals[3], 0, 16);
	if (keydata)
		memcpy(&vals[3], keydata, keylen);

	return ar->exec_cmd(ar, AR9170_CMD_EKEY,
			    sizeof(vals), (u8 *)vals,
			    1, &dummy);
}

int ar9170_disable_key(struct ar9170 *ar, u8 id)
{
	__le32 val = cpu_to_le32(id);
	u8 dummy;

	return ar->exec_cmd(ar, AR9170_CMD_EKEY,
			    sizeof(val), (u8 *)&val,
			    1, &dummy);
}
