/*
 * Firmware I/O code for mac80211 Prism54 drivers
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007-2009, Christian Lamparter <chunkeey@web.de>
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * Based on:
 * - the islsm (softmac prism54) driver, which is:
 *   Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 * - stlc45xx driver
 *   Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>

#include <net/mac80211.h>

#include "p54.h"
#include "eeprom.h"
#include "lmac.h"

int p54_parse_firmware(struct ieee80211_hw *dev, const struct firmware *fw)
{
	struct p54_common *priv = dev->priv;
	struct exp_if *exp_if;
	struct bootrec *bootrec;
	u32 *data = (u32 *)fw->data;
	u32 *end_data = (u32 *)fw->data + (fw->size >> 2);
	u8 *fw_version = NULL;
	size_t len;
	int i;
	int maxlen;

	if (priv->rx_start)
		return 0;

	while (data < end_data && *data)
		data++;

	while (data < end_data && !*data)
		data++;

	bootrec = (struct bootrec *) data;

	while (bootrec->data <= end_data && (bootrec->data +
	       (len = le32_to_cpu(bootrec->len))) <= end_data) {
		u32 code = le32_to_cpu(bootrec->code);
		switch (code) {
		case BR_CODE_COMPONENT_ID:
			priv->fw_interface = be32_to_cpup((__be32 *)
					     bootrec->data);
			switch (priv->fw_interface) {
			case FW_LM86:
			case FW_LM20:
			case FW_LM87: {
				char *iftype = (char *)bootrec->data;
				wiphy_info(priv->hw->wiphy,
					   "p54 detected a LM%c%c firmware\n",
					   iftype[2], iftype[3]);
				break;
				}
			case FW_FMAC:
			default:
				wiphy_err(priv->hw->wiphy,
					  "unsupported firmware\n");
				return -ENODEV;
			}
			break;
		case BR_CODE_COMPONENT_VERSION:
			/* 24 bytes should be enough for all firmwares */
			if (strnlen((unsigned char *) bootrec->data, 24) < 24)
				fw_version = (unsigned char *) bootrec->data;
			break;
		case BR_CODE_DESCR: {
			struct bootrec_desc *desc =
				(struct bootrec_desc *)bootrec->data;
			priv->rx_start = le32_to_cpu(desc->rx_start);
			/* FIXME add sanity checking */
			priv->rx_end = le32_to_cpu(desc->rx_end) - 0x3500;
			priv->headroom = desc->headroom;
			priv->tailroom = desc->tailroom;
			priv->privacy_caps = desc->privacy_caps;
			priv->rx_keycache_size = desc->rx_keycache_size;
			if (le32_to_cpu(bootrec->len) == 11)
				priv->rx_mtu = le16_to_cpu(desc->rx_mtu);
			else
				priv->rx_mtu = (size_t)
					0x620 - priv->tx_hdr_len;
			maxlen = priv->tx_hdr_len + /* USB devices */
				 sizeof(struct p54_rx_data) +
				 4 + /* rx alignment */
				 IEEE80211_MAX_FRAG_THRESHOLD;
			if (priv->rx_mtu > maxlen && PAGE_SIZE == 4096) {
				printk(KERN_INFO "p54: rx_mtu reduced from %d "
				       "to %d\n", priv->rx_mtu, maxlen);
				priv->rx_mtu = maxlen;
			}
			break;
			}
		case BR_CODE_EXPOSED_IF:
			exp_if = (struct exp_if *) bootrec->data;
			for (i = 0; i < (len * sizeof(*exp_if) / 4); i++)
				if (exp_if[i].if_id == cpu_to_le16(IF_ID_LMAC))
					priv->fw_var = le16_to_cpu(exp_if[i].variant);
			break;
		case BR_CODE_DEPENDENT_IF:
			break;
		case BR_CODE_END_OF_BRA:
		case LEGACY_BR_CODE_END_OF_BRA:
			end_data = NULL;
			break;
		default:
			break;
		}
		bootrec = (struct bootrec *)&bootrec->data[len];
	}

	if (fw_version)
		wiphy_info(priv->hw->wiphy,
			   "FW rev %s - Softmac protocol %x.%x\n",
			   fw_version, priv->fw_var >> 8, priv->fw_var & 0xff);

	if (priv->fw_var < 0x500)
		wiphy_info(priv->hw->wiphy,
			   "you are using an obsolete firmware. "
			   "visit http://wireless.kernel.org/en/users/Drivers/p54 "
			   "and grab one for \"kernel >= 2.6.28\"!\n");

	if (priv->fw_var >= 0x300) {
		/* Firmware supports QoS, use it! */

		if (priv->fw_var >= 0x500) {
			priv->tx_stats[P54_QUEUE_AC_VO].limit = 16;
			priv->tx_stats[P54_QUEUE_AC_VI].limit = 16;
			priv->tx_stats[P54_QUEUE_AC_BE].limit = 16;
			priv->tx_stats[P54_QUEUE_AC_BK].limit = 16;
		} else {
			priv->tx_stats[P54_QUEUE_AC_VO].limit = 3;
			priv->tx_stats[P54_QUEUE_AC_VI].limit = 4;
			priv->tx_stats[P54_QUEUE_AC_BE].limit = 3;
			priv->tx_stats[P54_QUEUE_AC_BK].limit = 2;
		}
		priv->hw->queues = P54_QUEUE_AC_NUM;
	}

	wiphy_info(priv->hw->wiphy,
		   "cryptographic accelerator WEP:%s, TKIP:%s, CCMP:%s\n",
		   (priv->privacy_caps & BR_DESC_PRIV_CAP_WEP) ? "YES" : "no",
		   (priv->privacy_caps &
		    (BR_DESC_PRIV_CAP_TKIP | BR_DESC_PRIV_CAP_MICHAEL))
		   ? "YES" : "no",
		   (priv->privacy_caps & BR_DESC_PRIV_CAP_AESCCMP)
		   ? "YES" : "no");

	if (priv->rx_keycache_size) {
		/*
		 * NOTE:
		 *
		 * The firmware provides at most 255 (0 - 254) slots
		 * for keys which are then used to offload decryption.
		 * As a result the 255 entry (aka 0xff) can be used
		 * safely by the driver to mark keys that didn't fit
		 * into the full cache. This trick saves us from
		 * keeping a extra list for uploaded keys.
		 */

		priv->used_rxkeys = kzalloc(BITS_TO_LONGS(
			priv->rx_keycache_size), GFP_KERNEL);

		if (!priv->used_rxkeys)
			return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(p54_parse_firmware);

static struct sk_buff *p54_alloc_skb(struct p54_common *priv, u16 hdr_flags,
				     u16 payload_len, u16 type, gfp_t memflags)
{
	struct p54_hdr *hdr;
	struct sk_buff *skb;
	size_t frame_len = sizeof(*hdr) + payload_len;

	if (frame_len > P54_MAX_CTRL_FRAME_LEN)
		return NULL;

	if (unlikely(skb_queue_len(&priv->tx_pending) > 64))
		return NULL;

	skb = __dev_alloc_skb(priv->tx_hdr_len + frame_len, memflags);
	if (!skb)
		return NULL;
	skb_reserve(skb, priv->tx_hdr_len);

	hdr = (struct p54_hdr *) skb_put(skb, sizeof(*hdr));
	hdr->flags = cpu_to_le16(hdr_flags);
	hdr->len = cpu_to_le16(payload_len);
	hdr->type = cpu_to_le16(type);
	hdr->tries = hdr->rts_tries = 0;
	return skb;
}

int p54_download_eeprom(struct p54_common *priv, void *buf,
			u16 offset, u16 len)
{
	struct p54_eeprom_lm86 *eeprom_hdr;
	struct sk_buff *skb;
	size_t eeprom_hdr_size;
	int ret = 0;

	if (priv->fw_var >= 0x509)
		eeprom_hdr_size = sizeof(*eeprom_hdr);
	else
		eeprom_hdr_size = 0x4;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL, eeprom_hdr_size +
			    len, P54_CONTROL_TYPE_EEPROM_READBACK,
			    GFP_KERNEL);
	if (unlikely(!skb))
		return -ENOMEM;

	mutex_lock(&priv->eeprom_mutex);
	priv->eeprom = buf;
	eeprom_hdr = (struct p54_eeprom_lm86 *) skb_put(skb,
		eeprom_hdr_size + len);

	if (priv->fw_var < 0x509) {
		eeprom_hdr->v1.offset = cpu_to_le16(offset);
		eeprom_hdr->v1.len = cpu_to_le16(len);
	} else {
		eeprom_hdr->v2.offset = cpu_to_le32(offset);
		eeprom_hdr->v2.len = cpu_to_le16(len);
		eeprom_hdr->v2.magic2 = 0xf;
		memcpy(eeprom_hdr->v2.magic, (const char *)"LOCK", 4);
	}

	p54_tx(priv, skb);

	if (!wait_for_completion_interruptible_timeout(
	     &priv->eeprom_comp, HZ)) {
		wiphy_err(priv->hw->wiphy, "device does not respond!\n");
		ret = -EBUSY;
	}
	priv->eeprom = NULL;
	mutex_unlock(&priv->eeprom_mutex);
	return ret;
}

int p54_update_beacon_tim(struct p54_common *priv, u16 aid, bool set)
{
	struct sk_buff *skb;
	struct p54_tim *tim;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*tim),
			    P54_CONTROL_TYPE_TIM, GFP_ATOMIC);
	if (unlikely(!skb))
		return -ENOMEM;

	tim = (struct p54_tim *) skb_put(skb, sizeof(*tim));
	tim->count = 1;
	tim->entry[0] = cpu_to_le16(set ? (aid | 0x8000) : aid);
	p54_tx(priv, skb);
	return 0;
}

int p54_sta_unlock(struct p54_common *priv, u8 *addr)
{
	struct sk_buff *skb;
	struct p54_sta_unlock *sta;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*sta),
			    P54_CONTROL_TYPE_PSM_STA_UNLOCK, GFP_ATOMIC);
	if (unlikely(!skb))
		return -ENOMEM;

	sta = (struct p54_sta_unlock *)skb_put(skb, sizeof(*sta));
	memcpy(sta->addr, addr, ETH_ALEN);
	p54_tx(priv, skb);
	return 0;
}

int p54_tx_cancel(struct p54_common *priv, __le32 req_id)
{
	struct sk_buff *skb;
	struct p54_txcancel *cancel;
	u32 _req_id = le32_to_cpu(req_id);

	if (unlikely(_req_id < priv->rx_start || _req_id > priv->rx_end))
		return -EINVAL;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*cancel),
			    P54_CONTROL_TYPE_TXCANCEL, GFP_ATOMIC);
	if (unlikely(!skb))
		return -ENOMEM;

	cancel = (struct p54_txcancel *)skb_put(skb, sizeof(*cancel));
	cancel->req_id = req_id;
	p54_tx(priv, skb);
	return 0;
}

int p54_setup_mac(struct p54_common *priv)
{
	struct sk_buff *skb;
	struct p54_setup_mac *setup;
	u16 mode;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*setup),
			    P54_CONTROL_TYPE_SETUP, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	setup = (struct p54_setup_mac *) skb_put(skb, sizeof(*setup));
	if (!(priv->hw->conf.flags & IEEE80211_CONF_IDLE)) {
		switch (priv->mode) {
		case NL80211_IFTYPE_STATION:
			mode = P54_FILTER_TYPE_STATION;
			break;
		case NL80211_IFTYPE_AP:
			mode = P54_FILTER_TYPE_AP;
			break;
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_MESH_POINT:
			mode = P54_FILTER_TYPE_IBSS;
			break;
		case NL80211_IFTYPE_MONITOR:
			mode = P54_FILTER_TYPE_PROMISCUOUS;
			break;
		default:
			mode = P54_FILTER_TYPE_HIBERNATE;
			break;
		}

		/*
		 * "TRANSPARENT and PROMISCUOUS are mutually exclusive"
		 * STSW45X0C LMAC API - page 12
		 */
		if (((priv->filter_flags & FIF_PROMISC_IN_BSS) ||
		     (priv->filter_flags & FIF_OTHER_BSS)) &&
		    (mode != P54_FILTER_TYPE_PROMISCUOUS))
			mode |= P54_FILTER_TYPE_TRANSPARENT;
	} else {
		mode = P54_FILTER_TYPE_HIBERNATE;
	}

	setup->mac_mode = cpu_to_le16(mode);
	memcpy(setup->mac_addr, priv->mac_addr, ETH_ALEN);
	memcpy(setup->bssid, priv->bssid, ETH_ALEN);
	setup->rx_antenna = 2 & priv->rx_diversity_mask; /* automatic */
	setup->rx_align = 0;
	if (priv->fw_var < 0x500) {
		setup->v1.basic_rate_mask = cpu_to_le32(priv->basic_rate_mask);
		memset(setup->v1.rts_rates, 0, 8);
		setup->v1.rx_addr = cpu_to_le32(priv->rx_end);
		setup->v1.max_rx = cpu_to_le16(priv->rx_mtu);
		setup->v1.rxhw = cpu_to_le16(priv->rxhw);
		setup->v1.wakeup_timer = cpu_to_le16(priv->wakeup_timer);
		setup->v1.unalloc0 = cpu_to_le16(0);
	} else {
		setup->v2.rx_addr = cpu_to_le32(priv->rx_end);
		setup->v2.max_rx = cpu_to_le16(priv->rx_mtu);
		setup->v2.rxhw = cpu_to_le16(priv->rxhw);
		setup->v2.timer = cpu_to_le16(priv->wakeup_timer);
		setup->v2.truncate = cpu_to_le16(48896);
		setup->v2.basic_rate_mask = cpu_to_le32(priv->basic_rate_mask);
		setup->v2.sbss_offset = 0;
		setup->v2.mcast_window = 0;
		setup->v2.rx_rssi_threshold = 0;
		setup->v2.rx_ed_threshold = 0;
		setup->v2.ref_clock = cpu_to_le32(644245094);
		setup->v2.lpf_bandwidth = cpu_to_le16(65535);
		setup->v2.osc_start_delay = cpu_to_le16(65535);
	}
	p54_tx(priv, skb);
	return 0;
}

int p54_scan(struct p54_common *priv, u16 mode, u16 dwell)
{
	struct sk_buff *skb;
	struct p54_hdr *hdr;
	struct p54_scan_head *head;
	struct p54_iq_autocal_entry *iq_autocal;
	union p54_scan_body_union *body;
	struct p54_scan_tail_rate *rate;
	struct pda_rssi_cal_entry *rssi;
	unsigned int i;
	void *entry;
	int band = priv->hw->conf.channel->band;
	__le16 freq = cpu_to_le16(priv->hw->conf.channel->center_freq);

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*head) +
			    2 + sizeof(*iq_autocal) + sizeof(*body) +
			    sizeof(*rate) + 2 * sizeof(*rssi),
			    P54_CONTROL_TYPE_SCAN, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	head = (struct p54_scan_head *) skb_put(skb, sizeof(*head));
	memset(head->scan_params, 0, sizeof(head->scan_params));
	head->mode = cpu_to_le16(mode);
	head->dwell = cpu_to_le16(dwell);
	head->freq = freq;

	if (priv->rxhw == PDR_SYNTH_FRONTEND_LONGBOW) {
		__le16 *pa_power_points = (__le16 *) skb_put(skb, 2);
		*pa_power_points = cpu_to_le16(0x0c);
	}

	iq_autocal = (void *) skb_put(skb, sizeof(*iq_autocal));
	for (i = 0; i < priv->iq_autocal_len; i++) {
		if (priv->iq_autocal[i].freq != freq)
			continue;

		memcpy(iq_autocal, &priv->iq_autocal[i].params,
		       sizeof(struct p54_iq_autocal_entry));
		break;
	}
	if (i == priv->iq_autocal_len)
		goto err;

	if (priv->rxhw == PDR_SYNTH_FRONTEND_LONGBOW)
		body = (void *) skb_put(skb, sizeof(body->longbow));
	else
		body = (void *) skb_put(skb, sizeof(body->normal));

	for (i = 0; i < priv->output_limit->entries; i++) {
		__le16 *entry_freq = (void *) (priv->output_limit->data +
				     priv->output_limit->entry_size * i);

		if (*entry_freq != freq)
			continue;

		if (priv->rxhw == PDR_SYNTH_FRONTEND_LONGBOW) {
			memcpy(&body->longbow.power_limits,
			       (void *) entry_freq + sizeof(__le16),
			       priv->output_limit->entry_size);
		} else {
			struct pda_channel_output_limit *limits =
			       (void *) entry_freq;

			body->normal.val_barker = 0x38;
			body->normal.val_bpsk = body->normal.dup_bpsk =
				limits->val_bpsk;
			body->normal.val_qpsk = body->normal.dup_qpsk =
				limits->val_qpsk;
			body->normal.val_16qam = body->normal.dup_16qam =
				limits->val_16qam;
			body->normal.val_64qam = body->normal.dup_64qam =
				limits->val_64qam;
		}
		break;
	}
	if (i == priv->output_limit->entries)
		goto err;

	entry = (void *)(priv->curve_data->data + priv->curve_data->offset);
	for (i = 0; i < priv->curve_data->entries; i++) {
		if (*((__le16 *)entry) != freq) {
			entry += priv->curve_data->entry_size;
			continue;
		}

		if (priv->rxhw == PDR_SYNTH_FRONTEND_LONGBOW) {
			memcpy(&body->longbow.curve_data,
				(void *) entry + sizeof(__le16),
				priv->curve_data->entry_size);
		} else {
			struct p54_scan_body *chan = &body->normal;
			struct pda_pa_curve_data *curve_data =
				(void *) priv->curve_data->data;

			entry += sizeof(__le16);
			chan->pa_points_per_curve = 8;
			memset(chan->curve_data, 0, sizeof(*chan->curve_data));
			memcpy(chan->curve_data, entry,
			       sizeof(struct p54_pa_curve_data_sample) *
			       min((u8)8, curve_data->points_per_channel));
		}
		break;
	}
	if (i == priv->curve_data->entries)
		goto err;

	if ((priv->fw_var >= 0x500) && (priv->fw_var < 0x509)) {
		rate = (void *) skb_put(skb, sizeof(*rate));
		rate->basic_rate_mask = cpu_to_le32(priv->basic_rate_mask);
		for (i = 0; i < sizeof(rate->rts_rates); i++)
			rate->rts_rates[i] = i;
	}

	rssi = (struct pda_rssi_cal_entry *) skb_put(skb, sizeof(*rssi));
	rssi->mul = cpu_to_le16(priv->rssical_db[band].mul);
	rssi->add = cpu_to_le16(priv->rssical_db[band].add);
	if (priv->rxhw == PDR_SYNTH_FRONTEND_LONGBOW) {
		/* Longbow frontend needs ever more */
		rssi = (void *) skb_put(skb, sizeof(*rssi));
		rssi->mul = cpu_to_le16(priv->rssical_db[band].longbow_unkn);
		rssi->add = cpu_to_le16(priv->rssical_db[band].longbow_unk2);
	}

	if (priv->fw_var >= 0x509) {
		rate = (void *) skb_put(skb, sizeof(*rate));
		rate->basic_rate_mask = cpu_to_le32(priv->basic_rate_mask);
		for (i = 0; i < sizeof(rate->rts_rates); i++)
			rate->rts_rates[i] = i;
	}

	hdr = (struct p54_hdr *) skb->data;
	hdr->len = cpu_to_le16(skb->len - sizeof(*hdr));

	p54_tx(priv, skb);
	return 0;

err:
	wiphy_err(priv->hw->wiphy, "frequency change to channel %d failed.\n",
		  ieee80211_frequency_to_channel(
			  priv->hw->conf.channel->center_freq));

	dev_kfree_skb_any(skb);
	return -EINVAL;
}

int p54_set_leds(struct p54_common *priv)
{
	struct sk_buff *skb;
	struct p54_led *led;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*led),
			    P54_CONTROL_TYPE_LED, GFP_ATOMIC);
	if (unlikely(!skb))
		return -ENOMEM;

	led = (struct p54_led *) skb_put(skb, sizeof(*led));
	led->flags = cpu_to_le16(0x0003);
	led->mask[0] = led->mask[1] = cpu_to_le16(priv->softled_state);
	led->delay[0] = cpu_to_le16(1);
	led->delay[1] = cpu_to_le16(0);
	p54_tx(priv, skb);
	return 0;
}

int p54_set_edcf(struct p54_common *priv)
{
	struct sk_buff *skb;
	struct p54_edcf *edcf;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*edcf),
			    P54_CONTROL_TYPE_DCFINIT, GFP_ATOMIC);
	if (unlikely(!skb))
		return -ENOMEM;

	edcf = (struct p54_edcf *)skb_put(skb, sizeof(*edcf));
	if (priv->use_short_slot) {
		edcf->slottime = 9;
		edcf->sifs = 0x10;
		edcf->eofpad = 0x00;
	} else {
		edcf->slottime = 20;
		edcf->sifs = 0x0a;
		edcf->eofpad = 0x06;
	}
	/* (see prism54/isl_oid.h for further details) */
	edcf->frameburst = cpu_to_le16(0);
	edcf->round_trip_delay = cpu_to_le16(0);
	edcf->flags = 0;
	memset(edcf->mapping, 0, sizeof(edcf->mapping));
	memcpy(edcf->queue, priv->qos_params, sizeof(edcf->queue));
	p54_tx(priv, skb);
	return 0;
}

int p54_set_ps(struct p54_common *priv)
{
	struct sk_buff *skb;
	struct p54_psm *psm;
	unsigned int i;
	u16 mode;

	if (priv->hw->conf.flags & IEEE80211_CONF_PS &&
	    !priv->powersave_override)
		mode = P54_PSM | P54_PSM_BEACON_TIMEOUT | P54_PSM_DTIM |
		       P54_PSM_CHECKSUM | P54_PSM_MCBC;
	else
		mode = P54_PSM_CAM;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*psm),
			    P54_CONTROL_TYPE_PSM, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	psm = (struct p54_psm *)skb_put(skb, sizeof(*psm));
	psm->mode = cpu_to_le16(mode);
	psm->aid = cpu_to_le16(priv->aid);
	for (i = 0; i < ARRAY_SIZE(psm->intervals); i++) {
		psm->intervals[i].interval =
			cpu_to_le16(priv->hw->conf.listen_interval);
		psm->intervals[i].periods = cpu_to_le16(1);
	}

	psm->beacon_rssi_skip_max = 200;
	psm->rssi_delta_threshold = 0;
	psm->nr = 1;
	psm->exclude[0] = WLAN_EID_TIM;

	p54_tx(priv, skb);
	return 0;
}

int p54_init_xbow_synth(struct p54_common *priv)
{
	struct sk_buff *skb;
	struct p54_xbow_synth *xbow;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*xbow),
			    P54_CONTROL_TYPE_XBOW_SYNTH_CFG, GFP_KERNEL);
	if (unlikely(!skb))
		return -ENOMEM;

	xbow = (struct p54_xbow_synth *)skb_put(skb, sizeof(*xbow));
	xbow->magic1 = cpu_to_le16(0x1);
	xbow->magic2 = cpu_to_le16(0x2);
	xbow->freq = cpu_to_le16(5390);
	memset(xbow->padding, 0, sizeof(xbow->padding));
	p54_tx(priv, skb);
	return 0;
}

int p54_upload_key(struct p54_common *priv, u8 algo, int slot, u8 idx, u8 len,
		   u8 *addr, u8* key)
{
	struct sk_buff *skb;
	struct p54_keycache *rxkey;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*rxkey),
			    P54_CONTROL_TYPE_RX_KEYCACHE, GFP_KERNEL);
	if (unlikely(!skb))
		return -ENOMEM;

	rxkey = (struct p54_keycache *)skb_put(skb, sizeof(*rxkey));
	rxkey->entry = slot;
	rxkey->key_id = idx;
	rxkey->key_type = algo;
	if (addr)
		memcpy(rxkey->mac, addr, ETH_ALEN);
	else
		memset(rxkey->mac, ~0, ETH_ALEN);

	switch (algo) {
	case P54_CRYPTO_WEP:
	case P54_CRYPTO_AESCCMP:
		rxkey->key_len = min_t(u8, 16, len);
		memcpy(rxkey->key, key, rxkey->key_len);
		break;

	case P54_CRYPTO_TKIPMICHAEL:
		rxkey->key_len = 24;
		memcpy(rxkey->key, key, 16);
		memcpy(&(rxkey->key[16]), &(key
			[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY]), 8);
		break;

	case P54_CRYPTO_NONE:
		rxkey->key_len = 0;
		memset(rxkey->key, 0, sizeof(rxkey->key));
		break;

	default:
		wiphy_err(priv->hw->wiphy,
			  "invalid cryptographic algorithm: %d\n", algo);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	p54_tx(priv, skb);
	return 0;
}

int p54_fetch_statistics(struct p54_common *priv)
{
	struct ieee80211_tx_info *txinfo;
	struct p54_tx_info *p54info;
	struct sk_buff *skb;

	skb = p54_alloc_skb(priv, P54_HDR_FLAG_CONTROL,
			    sizeof(struct p54_statistics),
			    P54_CONTROL_TYPE_STAT_READBACK, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	/*
	 * The statistic feedback causes some extra headaches here, if it
	 * is not to crash/corrupt the firmware data structures.
	 *
	 * Unlike all other Control Get OIDs we can not use helpers like
	 * skb_put to reserve the space for the data we're requesting.
	 * Instead the extra frame length -which will hold the results later-
	 * will only be told to the p54_assign_address, so that following
	 * frames won't be placed into the  allegedly empty area.
	 */
	txinfo = IEEE80211_SKB_CB(skb);
	p54info = (void *) txinfo->rate_driver_data;
	p54info->extra_len = sizeof(struct p54_statistics);

	p54_tx(priv, skb);
	return 0;
}
