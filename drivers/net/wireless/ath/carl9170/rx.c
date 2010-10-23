/*
 * Atheros CARL9170 driver
 *
 * 802.11 & command trap routines
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009, 2010, Christian Lamparter <chunkeey@googlemail.com>
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

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <net/mac80211.h>
#include "carl9170.h"
#include "hw.h"
#include "cmd.h"

static void carl9170_dbg_message(struct ar9170 *ar, const char *buf, u32 len)
{
	bool restart = false;
	enum carl9170_restart_reasons reason = CARL9170_RR_NO_REASON;

	if (len > 3) {
		if (memcmp(buf, CARL9170_ERR_MAGIC, 3) == 0) {
			ar->fw.err_counter++;
			if (ar->fw.err_counter > 3) {
				restart = true;
				reason = CARL9170_RR_TOO_MANY_FIRMWARE_ERRORS;
			}
		}

		if (memcmp(buf, CARL9170_BUG_MAGIC, 3) == 0) {
			ar->fw.bug_counter++;
			restart = true;
			reason = CARL9170_RR_FATAL_FIRMWARE_ERROR;
		}
	}

	wiphy_info(ar->hw->wiphy, "FW: %.*s\n", len, buf);

	if (restart)
		carl9170_restart(ar, reason);
}

static void carl9170_handle_ps(struct ar9170 *ar, struct carl9170_rsp *rsp)
{
	u32 ps;
	bool new_ps;

	ps = le32_to_cpu(rsp->psm.state);

	new_ps = (ps & CARL9170_PSM_COUNTER) != CARL9170_PSM_WAKE;
	if (ar->ps.state != new_ps) {
		if (!new_ps) {
			ar->ps.sleep_ms = jiffies_to_msecs(jiffies -
				ar->ps.last_action);
		}

		ar->ps.last_action = jiffies;

		ar->ps.state = new_ps;
	}
}

static int carl9170_check_sequence(struct ar9170 *ar, unsigned int seq)
{
	if (ar->cmd_seq < -1)
		return 0;

	/*
	 * Initialize Counter
	 */
	if (ar->cmd_seq < 0)
		ar->cmd_seq = seq;

	/*
	 * The sequence is strictly monotonic increasing and it never skips!
	 *
	 * Therefore we can safely assume that whenever we received an
	 * unexpected sequence we have lost some valuable data.
	 */
	if (seq != ar->cmd_seq) {
		int count;

		count = (seq - ar->cmd_seq) % ar->fw.cmd_bufs;

		wiphy_err(ar->hw->wiphy, "lost %d command responses/traps! "
			  "w:%d g:%d\n", count, ar->cmd_seq, seq);

		carl9170_restart(ar, CARL9170_RR_LOST_RSP);
		return -EIO;
	}

	ar->cmd_seq = (ar->cmd_seq + 1) % ar->fw.cmd_bufs;
	return 0;
}

static void carl9170_cmd_callback(struct ar9170 *ar, u32 len, void *buffer)
{
	/*
	 * Some commands may have a variable response length
	 * and we cannot predict the correct length in advance.
	 * So we only check if we provided enough space for the data.
	 */
	if (unlikely(ar->readlen != (len - 4))) {
		dev_warn(&ar->udev->dev, "received invalid command response:"
			 "got %d, instead of %d\n", len - 4, ar->readlen);
		print_hex_dump_bytes("carl9170 cmd:", DUMP_PREFIX_OFFSET,
			ar->cmd_buf, (ar->cmd.hdr.len + 4) & 0x3f);
		print_hex_dump_bytes("carl9170 rsp:", DUMP_PREFIX_OFFSET,
			buffer, len);
		/*
		 * Do not complete. The command times out,
		 * and we get a stack trace from there.
		 */
		carl9170_restart(ar, CARL9170_RR_INVALID_RSP);
	}

	spin_lock(&ar->cmd_lock);
	if (ar->readbuf) {
		if (len >= 4)
			memcpy(ar->readbuf, buffer + 4, len - 4);

		ar->readbuf = NULL;
	}
	complete(&ar->cmd_wait);
	spin_unlock(&ar->cmd_lock);
}

void carl9170_handle_command_response(struct ar9170 *ar, void *buf, u32 len)
{
	struct carl9170_rsp *cmd = (void *) buf;
	struct ieee80211_vif *vif;

	if (carl9170_check_sequence(ar, cmd->hdr.seq))
		return;

	if ((cmd->hdr.cmd & CARL9170_RSP_FLAG) != CARL9170_RSP_FLAG) {
		if (!(cmd->hdr.cmd & CARL9170_CMD_ASYNC_FLAG))
			carl9170_cmd_callback(ar, len, buf);

		return;
	}

	if (unlikely(cmd->hdr.len != (len - 4))) {
		if (net_ratelimit()) {
			wiphy_err(ar->hw->wiphy, "FW: received over-/under"
				"sized event %x (%d, but should be %d).\n",
			       cmd->hdr.cmd, cmd->hdr.len, len - 4);

			print_hex_dump_bytes("dump:", DUMP_PREFIX_NONE,
					     buf, len);
		}

		return;
	}

	/* hardware event handlers */
	switch (cmd->hdr.cmd) {
	case CARL9170_RSP_PRETBTT:
		/* pre-TBTT event */
		rcu_read_lock();
		vif = carl9170_get_main_vif(ar);

		if (!vif) {
			rcu_read_unlock();
			break;
		}

		switch (vif->type) {
		case NL80211_IFTYPE_STATION:
			carl9170_handle_ps(ar, cmd);
			break;

		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_ADHOC:
			carl9170_update_beacon(ar, true);
			break;

		default:
			break;
		}
		rcu_read_unlock();

		break;


	case CARL9170_RSP_TXCOMP:
		/* TX status notification */
		carl9170_tx_process_status(ar, cmd);
		break;

	case CARL9170_RSP_BEACON_CONFIG:
		/*
		 * (IBSS) beacon send notification
		 * bytes: 04 c2 XX YY B4 B3 B2 B1
		 *
		 * XX always 80
		 * YY always 00
		 * B1-B4 "should" be the number of send out beacons.
		 */
		break;

	case CARL9170_RSP_ATIM:
		/* End of Atim Window */
		break;

	case CARL9170_RSP_WATCHDOG:
		/* Watchdog Interrupt */
		carl9170_restart(ar, CARL9170_RR_WATCHDOG);
		break;

	case CARL9170_RSP_TEXT:
		/* firmware debug */
		carl9170_dbg_message(ar, (char *)buf + 4, len - 4);
		break;

	case CARL9170_RSP_HEXDUMP:
		wiphy_dbg(ar->hw->wiphy, "FW: HD %d\n", len - 4);
		print_hex_dump_bytes("FW:", DUMP_PREFIX_NONE,
				     (char *)buf + 4, len - 4);
		break;

	case CARL9170_RSP_RADAR:
		if (!net_ratelimit())
			break;

		wiphy_info(ar->hw->wiphy, "FW: RADAR! Please report this "
		       "incident to linux-wireless@vger.kernel.org !\n");
		break;

	case CARL9170_RSP_GPIO:
#ifdef CONFIG_CARL9170_WPC
		if (ar->wps.pbc) {
			bool state = !!(cmd->gpio.gpio & cpu_to_le32(
				AR9170_GPIO_PORT_WPS_BUTTON_PRESSED));

			if (state != ar->wps.pbc_state) {
				ar->wps.pbc_state = state;
				input_report_key(ar->wps.pbc, KEY_WPS_BUTTON,
						 state);
				input_sync(ar->wps.pbc);
			}
		}
#endif /* CONFIG_CARL9170_WPC */
		break;

	case CARL9170_RSP_BOOT:
		complete(&ar->fw_boot_wait);
		break;

	default:
		wiphy_err(ar->hw->wiphy, "FW: received unhandled event %x\n",
			cmd->hdr.cmd);
		print_hex_dump_bytes("dump:", DUMP_PREFIX_NONE, buf, len);
		break;
	}
}

static int carl9170_rx_mac_status(struct ar9170 *ar,
	struct ar9170_rx_head *head, struct ar9170_rx_macstatus *mac,
	struct ieee80211_rx_status *status)
{
	struct ieee80211_channel *chan;
	u8 error, decrypt;

	BUILD_BUG_ON(sizeof(struct ar9170_rx_head) != 12);
	BUILD_BUG_ON(sizeof(struct ar9170_rx_macstatus) != 4);

	error = mac->error;

	if (error & AR9170_RX_ERROR_WRONG_RA) {
		if (!ar->sniffer_enabled)
			return -EINVAL;
	}

	if (error & AR9170_RX_ERROR_PLCP) {
		if (!(ar->filter_state & FIF_PLCPFAIL))
			return -EINVAL;

		status->flag |= RX_FLAG_FAILED_PLCP_CRC;
	}

	if (error & AR9170_RX_ERROR_FCS) {
		ar->tx_fcs_errors++;

		if (!(ar->filter_state & FIF_FCSFAIL))
			return -EINVAL;

		status->flag |= RX_FLAG_FAILED_FCS_CRC;
	}

	decrypt = ar9170_get_decrypt_type(mac);
	if (!(decrypt & AR9170_RX_ENC_SOFTWARE) &&
	    decrypt != AR9170_ENC_ALG_NONE) {
		if ((decrypt == AR9170_ENC_ALG_TKIP) &&
		    (error & AR9170_RX_ERROR_MMIC))
			status->flag |= RX_FLAG_MMIC_ERROR;

		status->flag |= RX_FLAG_DECRYPTED;
	}

	if (error & AR9170_RX_ERROR_DECRYPT && !ar->sniffer_enabled)
		return -ENODATA;

	error &= ~(AR9170_RX_ERROR_MMIC |
		   AR9170_RX_ERROR_FCS |
		   AR9170_RX_ERROR_WRONG_RA |
		   AR9170_RX_ERROR_DECRYPT |
		   AR9170_RX_ERROR_PLCP);

	/* drop any other error frames */
	if (unlikely(error)) {
		/* TODO: update netdevice's RX dropped/errors statistics */

		if (net_ratelimit())
			wiphy_dbg(ar->hw->wiphy, "received frame with "
			       "suspicious error code (%#x).\n", error);

		return -EINVAL;
	}

	chan = ar->channel;
	if (chan) {
		status->band = chan->band;
		status->freq = chan->center_freq;
	}

	switch (mac->status & AR9170_RX_STATUS_MODULATION) {
	case AR9170_RX_STATUS_MODULATION_CCK:
		if (mac->status & AR9170_RX_STATUS_SHORT_PREAMBLE)
			status->flag |= RX_FLAG_SHORTPRE;
		switch (head->plcp[0]) {
		case AR9170_RX_PHY_RATE_CCK_1M:
			status->rate_idx = 0;
			break;
		case AR9170_RX_PHY_RATE_CCK_2M:
			status->rate_idx = 1;
			break;
		case AR9170_RX_PHY_RATE_CCK_5M:
			status->rate_idx = 2;
			break;
		case AR9170_RX_PHY_RATE_CCK_11M:
			status->rate_idx = 3;
			break;
		default:
			if (net_ratelimit()) {
				wiphy_err(ar->hw->wiphy, "invalid plcp cck "
				       "rate (%x).\n", head->plcp[0]);
			}

			return -EINVAL;
		}
		break;

	case AR9170_RX_STATUS_MODULATION_DUPOFDM:
	case AR9170_RX_STATUS_MODULATION_OFDM:
		switch (head->plcp[0] & 0xf) {
		case AR9170_TXRX_PHY_RATE_OFDM_6M:
			status->rate_idx = 0;
			break;
		case AR9170_TXRX_PHY_RATE_OFDM_9M:
			status->rate_idx = 1;
			break;
		case AR9170_TXRX_PHY_RATE_OFDM_12M:
			status->rate_idx = 2;
			break;
		case AR9170_TXRX_PHY_RATE_OFDM_18M:
			status->rate_idx = 3;
			break;
		case AR9170_TXRX_PHY_RATE_OFDM_24M:
			status->rate_idx = 4;
			break;
		case AR9170_TXRX_PHY_RATE_OFDM_36M:
			status->rate_idx = 5;
			break;
		case AR9170_TXRX_PHY_RATE_OFDM_48M:
			status->rate_idx = 6;
			break;
		case AR9170_TXRX_PHY_RATE_OFDM_54M:
			status->rate_idx = 7;
			break;
		default:
			if (net_ratelimit()) {
				wiphy_err(ar->hw->wiphy, "invalid plcp ofdm "
					"rate (%x).\n", head->plcp[0]);
			}

			return -EINVAL;
		}
		if (status->band == IEEE80211_BAND_2GHZ)
			status->rate_idx += 4;
		break;

	case AR9170_RX_STATUS_MODULATION_HT:
		if (head->plcp[3] & 0x80)
			status->flag |= RX_FLAG_40MHZ;
		if (head->plcp[6] & 0x80)
			status->flag |= RX_FLAG_SHORT_GI;

		status->rate_idx = clamp(0, 75, head->plcp[3] & 0x7f);
		status->flag |= RX_FLAG_HT;
		break;

	default:
		BUG();
		return -ENOSYS;
	}

	return 0;
}

static void carl9170_rx_phy_status(struct ar9170 *ar,
	struct ar9170_rx_phystatus *phy, struct ieee80211_rx_status *status)
{
	int i;

	BUILD_BUG_ON(sizeof(struct ar9170_rx_phystatus) != 20);

	for (i = 0; i < 3; i++)
		if (phy->rssi[i] != 0x80)
			status->antenna |= BIT(i);

	/* post-process RSSI */
	for (i = 0; i < 7; i++)
		if (phy->rssi[i] & 0x80)
			phy->rssi[i] = ((phy->rssi[i] & 0x7f) + 1) & 0x7f;

	/* TODO: we could do something with phy_errors */
	status->signal = ar->noise[0] + phy->rssi_combined;
}

static struct sk_buff *carl9170_rx_copy_data(u8 *buf, int len)
{
	struct sk_buff *skb;
	int reserved = 0;
	struct ieee80211_hdr *hdr = (void *) buf;

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		reserved += NET_IP_ALIGN;

		if (*qc & IEEE80211_QOS_CONTROL_A_MSDU_PRESENT)
			reserved += NET_IP_ALIGN;
	}

	if (ieee80211_has_a4(hdr->frame_control))
		reserved += NET_IP_ALIGN;

	reserved = 32 + (reserved & NET_IP_ALIGN);

	skb = dev_alloc_skb(len + reserved);
	if (likely(skb)) {
		skb_reserve(skb, reserved);
		memcpy(skb_put(skb, len), buf, len);
	}

	return skb;
}

static u8 *carl9170_find_ie(u8 *data, unsigned int len, u8 ie)
{
	struct ieee80211_mgmt *mgmt = (void *)data;
	u8 *pos, *end;

	pos = (u8 *)mgmt->u.beacon.variable;
	end = data + len;
	while (pos < end) {
		if (pos + 2 + pos[1] > end)
			return NULL;

		if (pos[0] == ie)
			return pos;

		pos += 2 + pos[1];
	}
	return NULL;
}

/*
 * NOTE:
 *
 * The firmware is in charge of waking up the device just before
 * the AP is expected to transmit the next beacon.
 *
 * This leaves the driver with the important task of deciding when
 * to set the PHY back to bed again.
 */
static void carl9170_ps_beacon(struct ar9170 *ar, void *data, unsigned int len)
{
	struct ieee80211_hdr *hdr = (void *) data;
	struct ieee80211_tim_ie *tim_ie;
	u8 *tim;
	u8 tim_len;
	bool cam;

	if (likely(!(ar->hw->conf.flags & IEEE80211_CONF_PS)))
		return;

	/* check if this really is a beacon */
	if (!ieee80211_is_beacon(hdr->frame_control))
		return;

	/* min. beacon length + FCS_LEN */
	if (len <= 40 + FCS_LEN)
		return;

	/* and only beacons from the associated BSSID, please */
	if (compare_ether_addr(hdr->addr3, ar->common.curbssid) ||
	    !ar->common.curaid)
		return;

	ar->ps.last_beacon = jiffies;

	tim = carl9170_find_ie(data, len - FCS_LEN, WLAN_EID_TIM);
	if (!tim)
		return;

	if (tim[1] < sizeof(*tim_ie))
		return;

	tim_len = tim[1];
	tim_ie = (struct ieee80211_tim_ie *) &tim[2];

	if (!WARN_ON_ONCE(!ar->hw->conf.ps_dtim_period))
		ar->ps.dtim_counter = (tim_ie->dtim_count - 1) %
			ar->hw->conf.ps_dtim_period;

	/* Check whenever the PHY can be turned off again. */

	/* 1. What about buffered unicast traffic for our AID? */
	cam = ieee80211_check_tim(tim_ie, tim_len, ar->common.curaid);

	/* 2. Maybe the AP wants to send multicast/broadcast data? */
	cam = !!(tim_ie->bitmap_ctrl & 0x01);

	if (!cam) {
		/* back to low-power land. */
		ar->ps.off_override &= ~PS_OFF_BCN;
		carl9170_ps_check(ar);
	} else {
		/* force CAM */
		ar->ps.off_override |= PS_OFF_BCN;
	}
}

static bool carl9170_ampdu_check(struct ar9170 *ar, u8 *buf, u8 ms)
{
	__le16 fc;

	if ((ms & AR9170_RX_STATUS_MPDU) == AR9170_RX_STATUS_MPDU_SINGLE) {
		/*
		 * This frame is not part of an aMPDU.
		 * Therefore it is not subjected to any
		 * of the following content restrictions.
		 */
		return true;
	}

	/*
	 * "802.11n - 7.4a.3 A-MPDU contents" describes in which contexts
	 * certain frame types can be part of an aMPDU.
	 *
	 * In order to keep the processing cost down, I opted for a
	 * stateless filter solely based on the frame control field.
	 */

	fc = ((struct ieee80211_hdr *)buf)->frame_control;
	if (ieee80211_is_data_qos(fc) && ieee80211_is_data_present(fc))
		return true;

	if (ieee80211_is_ack(fc) || ieee80211_is_back(fc) ||
	    ieee80211_is_back_req(fc))
		return true;

	if (ieee80211_is_action(fc))
		return true;

	return false;
}

/*
 * If the frame alignment is right (or the kernel has
 * CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS), and there
 * is only a single MPDU in the USB frame, then we could
 * submit to mac80211 the SKB directly. However, since
 * there may be multiple packets in one SKB in stream
 * mode, and we need to observe the proper ordering,
 * this is non-trivial.
 */

static void carl9170_handle_mpdu(struct ar9170 *ar, u8 *buf, int len)
{
	struct ar9170_rx_head *head;
	struct ar9170_rx_macstatus *mac;
	struct ar9170_rx_phystatus *phy = NULL;
	struct ieee80211_rx_status status;
	struct sk_buff *skb;
	int mpdu_len;
	u8 mac_status;

	if (!IS_STARTED(ar))
		return;

	if (unlikely(len < sizeof(*mac)))
		goto drop;

	mpdu_len = len - sizeof(*mac);

	mac = (void *)(buf + mpdu_len);
	mac_status = mac->status;
	switch (mac_status & AR9170_RX_STATUS_MPDU) {
	case AR9170_RX_STATUS_MPDU_FIRST:
		/* Aggregated MPDUs start with an PLCP header */
		if (likely(mpdu_len >= sizeof(struct ar9170_rx_head))) {
			head = (void *) buf;

			/*
			 * The PLCP header needs to be cached for the
			 * following MIDDLE + LAST A-MPDU packets.
			 *
			 * So, if you are wondering why all frames seem
			 * to share a common RX status information,
			 * then you have the answer right here...
			 */
			memcpy(&ar->rx_plcp, (void *) buf,
			       sizeof(struct ar9170_rx_head));

			mpdu_len -= sizeof(struct ar9170_rx_head);
			buf += sizeof(struct ar9170_rx_head);

			ar->rx_has_plcp = true;
		} else {
			if (net_ratelimit()) {
				wiphy_err(ar->hw->wiphy, "plcp info "
					"is clipped.\n");
			}

			goto drop;
		}
		break;

	case AR9170_RX_STATUS_MPDU_LAST:
		/*
		 * The last frame of an A-MPDU has an extra tail
		 * which does contain the phy status of the whole
		 * aggregate.
		 */

		if (likely(mpdu_len >= sizeof(struct ar9170_rx_phystatus))) {
			mpdu_len -= sizeof(struct ar9170_rx_phystatus);
			phy = (void *)(buf + mpdu_len);
		} else {
			if (net_ratelimit()) {
				wiphy_err(ar->hw->wiphy, "frame tail "
					"is clipped.\n");
			}

			goto drop;
		}

	case AR9170_RX_STATUS_MPDU_MIDDLE:
		/*  These are just data + mac status */
		if (unlikely(!ar->rx_has_plcp)) {
			if (!net_ratelimit())
				return;

			wiphy_err(ar->hw->wiphy, "rx stream does not start "
					"with a first_mpdu frame tag.\n");

			goto drop;
		}

		head = &ar->rx_plcp;
		break;

	case AR9170_RX_STATUS_MPDU_SINGLE:
		/* single mpdu has both: plcp (head) and phy status (tail) */
		head = (void *) buf;

		mpdu_len -= sizeof(struct ar9170_rx_head);
		mpdu_len -= sizeof(struct ar9170_rx_phystatus);

		buf += sizeof(struct ar9170_rx_head);
		phy = (void *)(buf + mpdu_len);
		break;

	default:
		BUG_ON(1);
		break;
	}

	/* FC + DU + RA + FCS */
	if (unlikely(mpdu_len < (2 + 2 + ETH_ALEN + FCS_LEN)))
		goto drop;

	memset(&status, 0, sizeof(status));
	if (unlikely(carl9170_rx_mac_status(ar, head, mac, &status)))
		goto drop;

	if (!carl9170_ampdu_check(ar, buf, mac_status))
		goto drop;

	if (phy)
		carl9170_rx_phy_status(ar, phy, &status);

	carl9170_ps_beacon(ar, buf, mpdu_len);

	skb = carl9170_rx_copy_data(buf, mpdu_len);
	if (!skb)
		goto drop;

	memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));
	ieee80211_rx(ar->hw, skb);
	return;

drop:
	ar->rx_dropped++;
}

static void carl9170_rx_untie_cmds(struct ar9170 *ar, const u8 *respbuf,
				   const unsigned int resplen)
{
	struct carl9170_rsp *cmd;
	int i = 0;

	while (i < resplen) {
		cmd = (void *) &respbuf[i];

		i += cmd->hdr.len + 4;
		if (unlikely(i > resplen))
			break;

		carl9170_handle_command_response(ar, cmd, cmd->hdr.len + 4);
	}

	if (unlikely(i != resplen)) {
		if (!net_ratelimit())
			return;

		wiphy_err(ar->hw->wiphy, "malformed firmware trap:\n");
		print_hex_dump_bytes("rxcmd:", DUMP_PREFIX_OFFSET,
				     respbuf, resplen);
	}
}

static void __carl9170_rx(struct ar9170 *ar, u8 *buf, unsigned int len)
{
	unsigned int i = 0;

	/* weird thing, but this is the same in the original driver */
	while (len > 2 && i < 12 && buf[0] == 0xff && buf[1] == 0xff) {
		i += 2;
		len -= 2;
		buf += 2;
	}

	if (unlikely(len < 4))
		return;

	/* found the 6 * 0xffff marker? */
	if (i == 12)
		carl9170_rx_untie_cmds(ar, buf, len);
	else
		carl9170_handle_mpdu(ar, buf, len);
}

static void carl9170_rx_stream(struct ar9170 *ar, void *buf, unsigned int len)
{
	unsigned int tlen, wlen = 0, clen = 0;
	struct ar9170_stream *rx_stream;
	u8 *tbuf;

	tbuf = buf;
	tlen = len;

	while (tlen >= 4) {
		rx_stream = (void *) tbuf;
		clen = le16_to_cpu(rx_stream->length);
		wlen = ALIGN(clen, 4);

		/* check if this is stream has a valid tag.*/
		if (rx_stream->tag != cpu_to_le16(AR9170_RX_STREAM_TAG)) {
			/*
			 * TODO: handle the highly unlikely event that the
			 * corrupted stream has the TAG at the right position.
			 */

			/* check if the frame can be repaired. */
			if (!ar->rx_failover_missing) {

				/* this is not "short read". */
				if (net_ratelimit()) {
					wiphy_err(ar->hw->wiphy,
						"missing tag!\n");
				}

				__carl9170_rx(ar, tbuf, tlen);
				return;
			}

			if (ar->rx_failover_missing > tlen) {
				if (net_ratelimit()) {
					wiphy_err(ar->hw->wiphy,
						"possible multi "
						"stream corruption!\n");
					goto err_telluser;
				} else {
					goto err_silent;
				}
			}

			memcpy(skb_put(ar->rx_failover, tlen), tbuf, tlen);
			ar->rx_failover_missing -= tlen;

			if (ar->rx_failover_missing <= 0) {
				/*
				 * nested carl9170_rx_stream call!
				 *
				 * termination is guranteed, even when the
				 * combined frame also have an element with
				 * a bad tag.
				 */

				ar->rx_failover_missing = 0;
				carl9170_rx_stream(ar, ar->rx_failover->data,
						   ar->rx_failover->len);

				skb_reset_tail_pointer(ar->rx_failover);
				skb_trim(ar->rx_failover, 0);
			}

			return;
		}

		/* check if stream is clipped */
		if (wlen > tlen - 4) {
			if (ar->rx_failover_missing) {
				/* TODO: handle double stream corruption. */
				if (net_ratelimit()) {
					wiphy_err(ar->hw->wiphy, "double rx "
						"stream corruption!\n");
					goto err_telluser;
				} else {
					goto err_silent;
				}
			}

			/*
			 * save incomplete data set.
			 * the firmware will resend the missing bits when
			 * the rx - descriptor comes round again.
			 */

			memcpy(skb_put(ar->rx_failover, tlen), tbuf, tlen);
			ar->rx_failover_missing = clen - tlen;
			return;
		}
		__carl9170_rx(ar, rx_stream->payload, clen);

		tbuf += wlen + 4;
		tlen -= wlen + 4;
	}

	if (tlen) {
		if (net_ratelimit()) {
			wiphy_err(ar->hw->wiphy, "%d bytes of unprocessed "
				"data left in rx stream!\n", tlen);
		}

		goto err_telluser;
	}

	return;

err_telluser:
	wiphy_err(ar->hw->wiphy, "damaged RX stream data [want:%d, "
		"data:%d, rx:%d, pending:%d ]\n", clen, wlen, tlen,
		ar->rx_failover_missing);

	if (ar->rx_failover_missing)
		print_hex_dump_bytes("rxbuf:", DUMP_PREFIX_OFFSET,
				     ar->rx_failover->data,
				     ar->rx_failover->len);

	print_hex_dump_bytes("stream:", DUMP_PREFIX_OFFSET,
			     buf, len);

	wiphy_err(ar->hw->wiphy, "please check your hardware and cables, if "
		"you see this message frequently.\n");

err_silent:
	if (ar->rx_failover_missing) {
		skb_reset_tail_pointer(ar->rx_failover);
		skb_trim(ar->rx_failover, 0);
		ar->rx_failover_missing = 0;
	}
}

void carl9170_rx(struct ar9170 *ar, void *buf, unsigned int len)
{
	if (ar->fw.rx_stream)
		carl9170_rx_stream(ar, buf, len);
	else
		__carl9170_rx(ar, buf, len);
}
