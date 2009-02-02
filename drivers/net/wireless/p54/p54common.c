/*
 * Common code for mac80211 Prism54 drivers
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007, Christian Lamparter <chunkeey@web.de>
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
#include <linux/firmware.h>
#include <linux/etherdevice.h>

#include <net/mac80211.h>

#include "p54.h"
#include "p54common.h"

static int modparam_nohwcrypt;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, S_IRUGO);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");
MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_DESCRIPTION("Softmac Prism54 common code");
MODULE_LICENSE("GPL");
MODULE_ALIAS("prism54common");

static struct ieee80211_rate p54_bgrates[] = {
	{ .bitrate = 10, .hw_value = 0, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 20, .hw_value = 1, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55, .hw_value = 2, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .hw_value = 3, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60, .hw_value = 4, },
	{ .bitrate = 90, .hw_value = 5, },
	{ .bitrate = 120, .hw_value = 6, },
	{ .bitrate = 180, .hw_value = 7, },
	{ .bitrate = 240, .hw_value = 8, },
	{ .bitrate = 360, .hw_value = 9, },
	{ .bitrate = 480, .hw_value = 10, },
	{ .bitrate = 540, .hw_value = 11, },
};

static struct ieee80211_channel p54_bgchannels[] = {
	{ .center_freq = 2412, .hw_value = 1, },
	{ .center_freq = 2417, .hw_value = 2, },
	{ .center_freq = 2422, .hw_value = 3, },
	{ .center_freq = 2427, .hw_value = 4, },
	{ .center_freq = 2432, .hw_value = 5, },
	{ .center_freq = 2437, .hw_value = 6, },
	{ .center_freq = 2442, .hw_value = 7, },
	{ .center_freq = 2447, .hw_value = 8, },
	{ .center_freq = 2452, .hw_value = 9, },
	{ .center_freq = 2457, .hw_value = 10, },
	{ .center_freq = 2462, .hw_value = 11, },
	{ .center_freq = 2467, .hw_value = 12, },
	{ .center_freq = 2472, .hw_value = 13, },
	{ .center_freq = 2484, .hw_value = 14, },
};

static struct ieee80211_supported_band band_2GHz = {
	.channels = p54_bgchannels,
	.n_channels = ARRAY_SIZE(p54_bgchannels),
	.bitrates = p54_bgrates,
	.n_bitrates = ARRAY_SIZE(p54_bgrates),
};

static struct ieee80211_rate p54_arates[] = {
	{ .bitrate = 60, .hw_value = 4, },
	{ .bitrate = 90, .hw_value = 5, },
	{ .bitrate = 120, .hw_value = 6, },
	{ .bitrate = 180, .hw_value = 7, },
	{ .bitrate = 240, .hw_value = 8, },
	{ .bitrate = 360, .hw_value = 9, },
	{ .bitrate = 480, .hw_value = 10, },
	{ .bitrate = 540, .hw_value = 11, },
};

static struct ieee80211_channel p54_achannels[] = {
	{ .center_freq = 4920 },
	{ .center_freq = 4940 },
	{ .center_freq = 4960 },
	{ .center_freq = 4980 },
	{ .center_freq = 5040 },
	{ .center_freq = 5060 },
	{ .center_freq = 5080 },
	{ .center_freq = 5170 },
	{ .center_freq = 5180 },
	{ .center_freq = 5190 },
	{ .center_freq = 5200 },
	{ .center_freq = 5210 },
	{ .center_freq = 5220 },
	{ .center_freq = 5230 },
	{ .center_freq = 5240 },
	{ .center_freq = 5260 },
	{ .center_freq = 5280 },
	{ .center_freq = 5300 },
	{ .center_freq = 5320 },
	{ .center_freq = 5500 },
	{ .center_freq = 5520 },
	{ .center_freq = 5540 },
	{ .center_freq = 5560 },
	{ .center_freq = 5580 },
	{ .center_freq = 5600 },
	{ .center_freq = 5620 },
	{ .center_freq = 5640 },
	{ .center_freq = 5660 },
	{ .center_freq = 5680 },
	{ .center_freq = 5700 },
	{ .center_freq = 5745 },
	{ .center_freq = 5765 },
	{ .center_freq = 5785 },
	{ .center_freq = 5805 },
	{ .center_freq = 5825 },
};

static struct ieee80211_supported_band band_5GHz = {
	.channels = p54_achannels,
	.n_channels = ARRAY_SIZE(p54_achannels),
	.bitrates = p54_arates,
	.n_bitrates = ARRAY_SIZE(p54_arates),
};

int p54_parse_firmware(struct ieee80211_hw *dev, const struct firmware *fw)
{
	struct p54_common *priv = dev->priv;
	struct bootrec_exp_if *exp_if;
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

	while (bootrec->data <= end_data &&
	       (bootrec->data + (len = le32_to_cpu(bootrec->len))) <= end_data) {
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
				printk(KERN_INFO "%s: p54 detected a LM%c%c "
						 "firmware\n",
					wiphy_name(dev->wiphy),
					iftype[2], iftype[3]);
				break;
				}
			case FW_FMAC:
			default:
				printk(KERN_ERR "%s: unsupported firmware\n",
					wiphy_name(dev->wiphy));
				return -ENODEV;
			}
			break;
		case BR_CODE_COMPONENT_VERSION:
			/* 24 bytes should be enough for all firmwares */
			if (strnlen((unsigned char*)bootrec->data, 24) < 24)
				fw_version = (unsigned char*)bootrec->data;
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
					         "to %d\n", priv->rx_mtu,
						 maxlen);
				priv->rx_mtu = maxlen;
			}
			break;
			}
		case BR_CODE_EXPOSED_IF:
			exp_if = (struct bootrec_exp_if *) bootrec->data;
			for (i = 0; i < (len * sizeof(*exp_if) / 4); i++)
				if (exp_if[i].if_id == cpu_to_le16(0x1a))
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
		printk(KERN_INFO "%s: FW rev %s - Softmac protocol %x.%x\n",
			wiphy_name(dev->wiphy), fw_version,
			priv->fw_var >> 8, priv->fw_var & 0xff);

	if (priv->fw_var < 0x500)
		printk(KERN_INFO "%s: you are using an obsolete firmware. "
		       "visit http://wireless.kernel.org/en/users/Drivers/p54 "
		       "and grab one for \"kernel >= 2.6.28\"!\n",
			wiphy_name(dev->wiphy));

	if (priv->fw_var >= 0x300) {
		/* Firmware supports QoS, use it! */
		priv->tx_stats[4].limit = 3;		/* AC_VO */
		priv->tx_stats[5].limit = 4;		/* AC_VI */
		priv->tx_stats[6].limit = 3;		/* AC_BE */
		priv->tx_stats[7].limit = 2;		/* AC_BK */
		dev->queues = 4;
	}

	if (!modparam_nohwcrypt)
		printk(KERN_INFO "%s: cryptographic accelerator "
				 "WEP:%s, TKIP:%s, CCMP:%s\n",
			wiphy_name(dev->wiphy),
			(priv->privacy_caps & BR_DESC_PRIV_CAP_WEP) ? "YES" :
			"no", (priv->privacy_caps & (BR_DESC_PRIV_CAP_TKIP |
			 BR_DESC_PRIV_CAP_MICHAEL)) ? "YES" : "no",
			(priv->privacy_caps & BR_DESC_PRIV_CAP_AESCCMP) ?
			"YES" : "no");

	return 0;
}
EXPORT_SYMBOL_GPL(p54_parse_firmware);

static int p54_convert_rev0(struct ieee80211_hw *dev,
			    struct pda_pa_curve_data *curve_data)
{
	struct p54_common *priv = dev->priv;
	struct p54_pa_curve_data_sample *dst;
	struct pda_pa_curve_data_sample_rev0 *src;
	size_t cd_len = sizeof(*curve_data) +
		(curve_data->points_per_channel*sizeof(*dst) + 2) *
		 curve_data->channels;
	unsigned int i, j;
	void *source, *target;

	priv->curve_data = kmalloc(cd_len, GFP_KERNEL);
	if (!priv->curve_data)
		return -ENOMEM;

	memcpy(priv->curve_data, curve_data, sizeof(*curve_data));
	source = curve_data->data;
	target = priv->curve_data->data;
	for (i = 0; i < curve_data->channels; i++) {
		__le16 *freq = source;
		source += sizeof(__le16);
		*((__le16 *)target) = *freq;
		target += sizeof(__le16);
		for (j = 0; j < curve_data->points_per_channel; j++) {
			dst = target;
			src = source;

			dst->rf_power = src->rf_power;
			dst->pa_detector = src->pa_detector;
			dst->data_64qam = src->pcv;
			/* "invent" the points for the other modulations */
#define SUB(x,y) (u8)((x) - (y)) > (x) ? 0 : (x) - (y)
			dst->data_16qam = SUB(src->pcv, 12);
			dst->data_qpsk = SUB(dst->data_16qam, 12);
			dst->data_bpsk = SUB(dst->data_qpsk, 12);
			dst->data_barker = SUB(dst->data_bpsk, 14);
#undef SUB
			target += sizeof(*dst);
			source += sizeof(*src);
		}
	}

	return 0;
}

static int p54_convert_rev1(struct ieee80211_hw *dev,
			    struct pda_pa_curve_data *curve_data)
{
	struct p54_common *priv = dev->priv;
	struct p54_pa_curve_data_sample *dst;
	struct pda_pa_curve_data_sample_rev1 *src;
	size_t cd_len = sizeof(*curve_data) +
		(curve_data->points_per_channel*sizeof(*dst) + 2) *
		 curve_data->channels;
	unsigned int i, j;
	void *source, *target;

	priv->curve_data = kmalloc(cd_len, GFP_KERNEL);
	if (!priv->curve_data)
		return -ENOMEM;

	memcpy(priv->curve_data, curve_data, sizeof(*curve_data));
	source = curve_data->data;
	target = priv->curve_data->data;
	for (i = 0; i < curve_data->channels; i++) {
		__le16 *freq = source;
		source += sizeof(__le16);
		*((__le16 *)target) = *freq;
		target += sizeof(__le16);
		for (j = 0; j < curve_data->points_per_channel; j++) {
			memcpy(target, source, sizeof(*src));

			target += sizeof(*dst);
			source += sizeof(*src);
		}
		source++;
	}

	return 0;
}

static const char *p54_rf_chips[] = { "NULL", "Duette3", "Duette2",
                              "Frisbee", "Xbow", "Longbow", "NULL", "NULL" };
static int p54_init_xbow_synth(struct ieee80211_hw *dev);

static void p54_parse_rssical(struct ieee80211_hw *dev, void *data, int len,
			     u16 type)
{
	struct p54_common *priv = dev->priv;
	int offset = (type == PDR_RSSI_LINEAR_APPROXIMATION_EXTENDED) ? 2 : 0;
	int entry_size = sizeof(struct pda_rssi_cal_entry) + offset;
	int num_entries = (type == PDR_RSSI_LINEAR_APPROXIMATION) ? 1 : 2;
	int i;

	if (len != (entry_size * num_entries)) {
		printk(KERN_ERR "%s: unknown rssi calibration data packing "
				 " type:(%x) len:%d.\n",
		       wiphy_name(dev->wiphy), type, len);

		print_hex_dump_bytes("rssical:", DUMP_PREFIX_NONE,
				     data, len);

		printk(KERN_ERR "%s: please report this issue.\n",
			wiphy_name(dev->wiphy));
		return;
	}

	for (i = 0; i < num_entries; i++) {
		struct pda_rssi_cal_entry *cal = data +
						 (offset + i * entry_size);
		priv->rssical_db[i].mul = (s16) le16_to_cpu(cal->mul);
		priv->rssical_db[i].add = (s16) le16_to_cpu(cal->add);
	}
}

static int p54_parse_eeprom(struct ieee80211_hw *dev, void *eeprom, int len)
{
	struct p54_common *priv = dev->priv;
	struct eeprom_pda_wrap *wrap = NULL;
	struct pda_entry *entry;
	unsigned int data_len, entry_len;
	void *tmp;
	int err;
	u8 *end = (u8 *)eeprom + len;
	u16 synth = 0;

	wrap = (struct eeprom_pda_wrap *) eeprom;
	entry = (void *)wrap->data + le16_to_cpu(wrap->len);

	/* verify that at least the entry length/code fits */
	while ((u8 *)entry <= end - sizeof(*entry)) {
		entry_len = le16_to_cpu(entry->len);
		data_len = ((entry_len - 1) << 1);

		/* abort if entry exceeds whole structure */
		if ((u8 *)entry + sizeof(*entry) + data_len > end)
			break;

		switch (le16_to_cpu(entry->code)) {
		case PDR_MAC_ADDRESS:
			SET_IEEE80211_PERM_ADDR(dev, entry->data);
			break;
		case PDR_PRISM_PA_CAL_OUTPUT_POWER_LIMITS:
			if (data_len < 2) {
				err = -EINVAL;
				goto err;
			}

			if (2 + entry->data[1]*sizeof(*priv->output_limit) > data_len) {
				err = -EINVAL;
				goto err;
			}

			priv->output_limit = kmalloc(entry->data[1] *
				sizeof(*priv->output_limit), GFP_KERNEL);

			if (!priv->output_limit) {
				err = -ENOMEM;
				goto err;
			}

			memcpy(priv->output_limit, &entry->data[2],
			       entry->data[1]*sizeof(*priv->output_limit));
			priv->output_limit_len = entry->data[1];
			break;
		case PDR_PRISM_PA_CAL_CURVE_DATA: {
			struct pda_pa_curve_data *curve_data =
				(struct pda_pa_curve_data *)entry->data;
			if (data_len < sizeof(*curve_data)) {
				err = -EINVAL;
				goto err;
			}

			switch (curve_data->cal_method_rev) {
			case 0:
				err = p54_convert_rev0(dev, curve_data);
				break;
			case 1:
				err = p54_convert_rev1(dev, curve_data);
				break;
			default:
				printk(KERN_ERR "%s: unknown curve data "
						"revision %d\n",
						wiphy_name(dev->wiphy),
						curve_data->cal_method_rev);
				err = -ENODEV;
				break;
			}
			if (err)
				goto err;
			}
			break;
		case PDR_PRISM_ZIF_TX_IQ_CALIBRATION:
			priv->iq_autocal = kmalloc(data_len, GFP_KERNEL);
			if (!priv->iq_autocal) {
				err = -ENOMEM;
				goto err;
			}

			memcpy(priv->iq_autocal, entry->data, data_len);
			priv->iq_autocal_len = data_len / sizeof(struct pda_iq_autocal_entry);
			break;
		case PDR_INTERFACE_LIST:
			tmp = entry->data;
			while ((u8 *)tmp < entry->data + data_len) {
				struct bootrec_exp_if *exp_if = tmp;
				if (le16_to_cpu(exp_if->if_id) == 0xf)
					synth = le16_to_cpu(exp_if->variant);
				tmp += sizeof(struct bootrec_exp_if);
			}
			break;
		case PDR_HARDWARE_PLATFORM_COMPONENT_ID:
			priv->version = *(u8 *)(entry->data + 1);
			break;
		case PDR_RSSI_LINEAR_APPROXIMATION:
		case PDR_RSSI_LINEAR_APPROXIMATION_DUAL_BAND:
		case PDR_RSSI_LINEAR_APPROXIMATION_EXTENDED:
			p54_parse_rssical(dev, entry->data, data_len,
					  le16_to_cpu(entry->code));
			break;
		case PDR_END:
			/* make it overrun */
			entry_len = len;
			break;
		case PDR_MANUFACTURING_PART_NUMBER:
		case PDR_PDA_VERSION:
		case PDR_NIC_SERIAL_NUMBER:
		case PDR_REGULATORY_DOMAIN_LIST:
		case PDR_TEMPERATURE_TYPE:
		case PDR_PRISM_PCI_IDENTIFIER:
		case PDR_COUNTRY_INFORMATION:
		case PDR_OEM_NAME:
		case PDR_PRODUCT_NAME:
		case PDR_UTF8_OEM_NAME:
		case PDR_UTF8_PRODUCT_NAME:
		case PDR_COUNTRY_LIST:
		case PDR_DEFAULT_COUNTRY:
		case PDR_ANTENNA_GAIN:
		case PDR_PRISM_INDIGO_PA_CALIBRATION_DATA:
		case PDR_REGULATORY_POWER_LIMITS:
		case PDR_RADIATED_TRANSMISSION_CORRECTION:
		case PDR_PRISM_TX_IQ_CALIBRATION:
		case PDR_BASEBAND_REGISTERS:
		case PDR_PER_CHANNEL_BASEBAND_REGISTERS:
			break;
		default:
			printk(KERN_INFO "%s: unknown eeprom code : 0x%x\n",
				wiphy_name(dev->wiphy),
				le16_to_cpu(entry->code));
			break;
		}

		entry = (void *)entry + (entry_len + 1)*2;
	}

	if (!synth || !priv->iq_autocal || !priv->output_limit ||
	    !priv->curve_data) {
		printk(KERN_ERR "%s: not all required entries found in eeprom!\n",
			wiphy_name(dev->wiphy));
		err = -EINVAL;
		goto err;
	}

	priv->rxhw = synth & PDR_SYNTH_FRONTEND_MASK;
	if (priv->rxhw == 4)
		p54_init_xbow_synth(dev);
	if (!(synth & PDR_SYNTH_24_GHZ_DISABLED))
		dev->wiphy->bands[IEEE80211_BAND_2GHZ] = &band_2GHz;
	if (!(synth & PDR_SYNTH_5_GHZ_DISABLED))
		dev->wiphy->bands[IEEE80211_BAND_5GHZ] = &band_5GHz;

	if (!is_valid_ether_addr(dev->wiphy->perm_addr)) {
		u8 perm_addr[ETH_ALEN];

		printk(KERN_WARNING "%s: Invalid hwaddr! Using randomly generated MAC addr\n",
			wiphy_name(dev->wiphy));
		random_ether_addr(perm_addr);
		SET_IEEE80211_PERM_ADDR(dev, perm_addr);
	}

	printk(KERN_INFO "%s: hwaddr %pM, MAC:isl38%02x RF:%s\n",
		wiphy_name(dev->wiphy),
		dev->wiphy->perm_addr,
		priv->version, p54_rf_chips[priv->rxhw]);

	return 0;

  err:
	if (priv->iq_autocal) {
		kfree(priv->iq_autocal);
		priv->iq_autocal = NULL;
	}

	if (priv->output_limit) {
		kfree(priv->output_limit);
		priv->output_limit = NULL;
	}

	if (priv->curve_data) {
		kfree(priv->curve_data);
		priv->curve_data = NULL;
	}

	printk(KERN_ERR "%s: eeprom parse failed!\n",
		wiphy_name(dev->wiphy));
	return err;
}

static int p54_rssi_to_dbm(struct ieee80211_hw *dev, int rssi)
{
	struct p54_common *priv = dev->priv;
	int band = dev->conf.channel->band;

	return ((rssi * priv->rssical_db[band].mul) / 64 +
			 priv->rssical_db[band].add) / 4;
}

static int p54_rx_data(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_common *priv = dev->priv;
	struct p54_rx_data *hdr = (struct p54_rx_data *) skb->data;
	struct ieee80211_rx_status rx_status = {0};
	u16 freq = le16_to_cpu(hdr->freq);
	size_t header_len = sizeof(*hdr);
	u32 tsf32;
	u8 rate = hdr->rate & 0xf;

	/*
	 * If the device is in a unspecified state we have to
	 * ignore all data frames. Else we could end up with a
	 * nasty crash.
	 */
	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED))
		return 0;

	if (!(hdr->flags & cpu_to_le16(P54_HDR_FLAG_DATA_IN_FCS_GOOD))) {
		if (priv->filter_flags & FIF_FCSFAIL)
			rx_status.flag |= RX_FLAG_FAILED_FCS_CRC;
		else
			return 0;
	}

	if (hdr->decrypt_status == P54_DECRYPT_OK)
		rx_status.flag |= RX_FLAG_DECRYPTED;
	if ((hdr->decrypt_status == P54_DECRYPT_FAIL_MICHAEL) ||
	    (hdr->decrypt_status == P54_DECRYPT_FAIL_TKIP))
		rx_status.flag |= RX_FLAG_MMIC_ERROR;

	rx_status.signal = p54_rssi_to_dbm(dev, hdr->rssi);
	rx_status.noise = priv->noise;
	/* XX correct? */
	rx_status.qual = (100 * hdr->rssi) / 127;
	if (hdr->rate & 0x10)
		rx_status.flag |= RX_FLAG_SHORTPRE;
	if (dev->conf.channel->band == IEEE80211_BAND_5GHZ)
		rx_status.rate_idx = (rate < 4) ? 0 : rate - 4;
	else
		rx_status.rate_idx = rate;

	rx_status.freq = freq;
	rx_status.band =  dev->conf.channel->band;
	rx_status.antenna = hdr->antenna;

	tsf32 = le32_to_cpu(hdr->tsf32);
	if (tsf32 < priv->tsf_low32)
		priv->tsf_high32++;
	rx_status.mactime = ((u64)priv->tsf_high32) << 32 | tsf32;
	priv->tsf_low32 = tsf32;

	rx_status.flag |= RX_FLAG_TSFT;

	if (hdr->flags & cpu_to_le16(P54_HDR_FLAG_DATA_ALIGN))
		header_len += hdr->align[0];

	skb_pull(skb, header_len);
	skb_trim(skb, le16_to_cpu(hdr->len));

	ieee80211_rx_irqsafe(dev, skb, &rx_status);

	queue_delayed_work(dev->workqueue, &priv->work,
			   msecs_to_jiffies(P54_STATISTICS_UPDATE));

	return -1;
}

static void inline p54_wake_free_queues(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	int i;

	if (priv->mode == NL80211_IFTYPE_UNSPECIFIED)
		return ;

	for (i = 0; i < dev->queues; i++)
		if (priv->tx_stats[i + 4].len < priv->tx_stats[i + 4].limit)
			ieee80211_wake_queue(dev, i);
}

void p54_free_skb(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_common *priv = dev->priv;
	struct ieee80211_tx_info *info;
	struct memrecord *range;
	unsigned long flags;
	u32 freed = 0, last_addr = priv->rx_start;

	if (unlikely(!skb || !dev || !skb_queue_len(&priv->tx_queue)))
		return;

	/*
	 * don't try to free an already unlinked skb
	 */
	if (unlikely((!skb->next) || (!skb->prev)))
		return;

	spin_lock_irqsave(&priv->tx_queue.lock, flags);
	info = IEEE80211_SKB_CB(skb);
	range = (void *)info->rate_driver_data;
	if (skb->prev != (struct sk_buff *)&priv->tx_queue) {
		struct ieee80211_tx_info *ni;
		struct memrecord *mr;

		ni = IEEE80211_SKB_CB(skb->prev);
		mr = (struct memrecord *)ni->rate_driver_data;
		last_addr = mr->end_addr;
	}
	if (skb->next != (struct sk_buff *)&priv->tx_queue) {
		struct ieee80211_tx_info *ni;
		struct memrecord *mr;

		ni = IEEE80211_SKB_CB(skb->next);
		mr = (struct memrecord *)ni->rate_driver_data;
		freed = mr->start_addr - last_addr;
	} else
		freed = priv->rx_end - last_addr;
	__skb_unlink(skb, &priv->tx_queue);
	spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
	dev_kfree_skb_any(skb);

	if (freed >= priv->headroom + sizeof(struct p54_hdr) + 48 +
		     IEEE80211_MAX_RTS_THRESHOLD + priv->tailroom)
		p54_wake_free_queues(dev);
}
EXPORT_SYMBOL_GPL(p54_free_skb);

static struct sk_buff *p54_find_tx_entry(struct ieee80211_hw *dev,
					   __le32 req_id)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *entry = priv->tx_queue.next;
	unsigned long flags;

	spin_lock_irqsave(&priv->tx_queue.lock, flags);
	while (entry != (struct sk_buff *)&priv->tx_queue) {
		struct p54_hdr *hdr = (struct p54_hdr *) entry->data;

		if (hdr->req_id == req_id) {
			spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
			return entry;
		}
		entry = entry->next;
	}
	spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
	return NULL;
}

static void p54_rx_frame_sent(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_common *priv = dev->priv;
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;
	struct p54_frame_sent *payload = (struct p54_frame_sent *) hdr->data;
	struct sk_buff *entry = (struct sk_buff *) priv->tx_queue.next;
	u32 addr = le32_to_cpu(hdr->req_id) - priv->headroom;
	struct memrecord *range = NULL;
	u32 freed = 0;
	u32 last_addr = priv->rx_start;
	unsigned long flags;
	int count, idx;

	spin_lock_irqsave(&priv->tx_queue.lock, flags);
	while (entry != (struct sk_buff *)&priv->tx_queue) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(entry);
		struct p54_hdr *entry_hdr;
		struct p54_tx_data *entry_data;
		unsigned int pad = 0, frame_len;

		range = (void *)info->rate_driver_data;
		if (range->start_addr != addr) {
			last_addr = range->end_addr;
			entry = entry->next;
			continue;
		}

		if (entry->next != (struct sk_buff *)&priv->tx_queue) {
			struct ieee80211_tx_info *ni;
			struct memrecord *mr;

			ni = IEEE80211_SKB_CB(entry->next);
			mr = (struct memrecord *)ni->rate_driver_data;
			freed = mr->start_addr - last_addr;
		} else
			freed = priv->rx_end - last_addr;

		last_addr = range->end_addr;
		__skb_unlink(entry, &priv->tx_queue);
		spin_unlock_irqrestore(&priv->tx_queue.lock, flags);

		frame_len = entry->len;
		entry_hdr = (struct p54_hdr *) entry->data;
		entry_data = (struct p54_tx_data *) entry_hdr->data;
		priv->tx_stats[entry_data->hw_queue].len--;
		priv->stats.dot11ACKFailureCount += payload->tries - 1;

		if (unlikely(entry == priv->cached_beacon)) {
			kfree_skb(entry);
			priv->cached_beacon = NULL;
			goto out;
		}

		/*
		 * Clear manually, ieee80211_tx_info_clear_status would
		 * clear the counts too and we need them.
		 */
		memset(&info->status.ampdu_ack_len, 0,
		       sizeof(struct ieee80211_tx_info) -
		       offsetof(struct ieee80211_tx_info, status.ampdu_ack_len));
		BUILD_BUG_ON(offsetof(struct ieee80211_tx_info,
				      status.ampdu_ack_len) != 23);

		if (entry_hdr->flags & cpu_to_le16(P54_HDR_FLAG_DATA_ALIGN))
			pad = entry_data->align[0];

		/* walk through the rates array and adjust the counts */
		count = payload->tries;
		for (idx = 0; idx < 4; idx++) {
			if (count >= info->status.rates[idx].count) {
				count -= info->status.rates[idx].count;
			} else if (count > 0) {
				info->status.rates[idx].count = count;
				count = 0;
			} else {
				info->status.rates[idx].idx = -1;
				info->status.rates[idx].count = 0;
			}
		}

		if (!(info->flags & IEEE80211_TX_CTL_NO_ACK) &&
		     (!payload->status))
			info->flags |= IEEE80211_TX_STAT_ACK;
		if (payload->status & P54_TX_PSM_CANCELLED)
			info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
		info->status.ack_signal = p54_rssi_to_dbm(dev,
				(int)payload->ack_rssi);

		/* Undo all changes to the frame. */
		switch (entry_data->key_type) {
		case P54_CRYPTO_TKIPMICHAEL: {
			u8 *iv = (u8 *)(entry_data->align + pad +
					entry_data->crypt_offset);

			/* Restore the original TKIP IV. */
			iv[2] = iv[0];
			iv[0] = iv[1];
			iv[1] = (iv[0] | 0x20) & 0x7f;	/* WEPSeed - 8.3.2.2 */

			frame_len -= 12; /* remove TKIP_MMIC + TKIP_ICV */
			break;
			}
		case P54_CRYPTO_AESCCMP:
			frame_len -= 8; /* remove CCMP_MIC */
			break;
		case P54_CRYPTO_WEP:
			frame_len -= 4; /* remove WEP_ICV */
			break;
		}
		skb_trim(entry, frame_len);
		skb_pull(entry, sizeof(*hdr) + pad + sizeof(*entry_data));
		ieee80211_tx_status_irqsafe(dev, entry);
		goto out;
	}
	spin_unlock_irqrestore(&priv->tx_queue.lock, flags);

out:
	if (freed >= priv->headroom + sizeof(struct p54_hdr) + 48 +
		     IEEE80211_MAX_RTS_THRESHOLD + priv->tailroom)
		p54_wake_free_queues(dev);
}

static void p54_rx_eeprom_readback(struct ieee80211_hw *dev,
				   struct sk_buff *skb)
{
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;
	struct p54_eeprom_lm86 *eeprom = (struct p54_eeprom_lm86 *) hdr->data;
	struct p54_common *priv = dev->priv;

	if (!priv->eeprom)
		return ;

	if (priv->fw_var >= 0x509) {
		memcpy(priv->eeprom, eeprom->v2.data,
		       le16_to_cpu(eeprom->v2.len));
	} else {
		memcpy(priv->eeprom, eeprom->v1.data,
		       le16_to_cpu(eeprom->v1.len));
	}

	complete(&priv->eeprom_comp);
}

static void p54_rx_stats(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_common *priv = dev->priv;
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;
	struct p54_statistics *stats = (struct p54_statistics *) hdr->data;
	u32 tsf32;

	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED))
		return ;

	tsf32 = le32_to_cpu(stats->tsf32);
	if (tsf32 < priv->tsf_low32)
		priv->tsf_high32++;
	priv->tsf_low32 = tsf32;

	priv->stats.dot11RTSFailureCount = le32_to_cpu(stats->rts_fail);
	priv->stats.dot11RTSSuccessCount = le32_to_cpu(stats->rts_success);
	priv->stats.dot11FCSErrorCount = le32_to_cpu(stats->rx_bad_fcs);

	priv->noise = p54_rssi_to_dbm(dev, le32_to_cpu(stats->noise));

	p54_free_skb(dev, p54_find_tx_entry(dev, hdr->req_id));
}

static void p54_rx_trap(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;
	struct p54_trap *trap = (struct p54_trap *) hdr->data;
	u16 event = le16_to_cpu(trap->event);
	u16 freq = le16_to_cpu(trap->frequency);

	switch (event) {
	case P54_TRAP_BEACON_TX:
		break;
	case P54_TRAP_RADAR:
		printk(KERN_INFO "%s: radar (freq:%d MHz)\n",
			wiphy_name(dev->wiphy), freq);
		break;
	case P54_TRAP_NO_BEACON:
		break;
	case P54_TRAP_SCAN:
		break;
	case P54_TRAP_TBTT:
		break;
	case P54_TRAP_TIMER:
		break;
	default:
		printk(KERN_INFO "%s: received event:%x freq:%d\n",
		       wiphy_name(dev->wiphy), event, freq);
		break;
	}
}

static int p54_rx_control(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;

	switch (le16_to_cpu(hdr->type)) {
	case P54_CONTROL_TYPE_TXDONE:
		p54_rx_frame_sent(dev, skb);
		break;
	case P54_CONTROL_TYPE_TRAP:
		p54_rx_trap(dev, skb);
		break;
	case P54_CONTROL_TYPE_BBP:
		break;
	case P54_CONTROL_TYPE_STAT_READBACK:
		p54_rx_stats(dev, skb);
		break;
	case P54_CONTROL_TYPE_EEPROM_READBACK:
		p54_rx_eeprom_readback(dev, skb);
		break;
	default:
		printk(KERN_DEBUG "%s: not handling 0x%02x type control frame\n",
		       wiphy_name(dev->wiphy), le16_to_cpu(hdr->type));
		break;
	}

	return 0;
}

/* returns zero if skb can be reused */
int p54_rx(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	u16 type = le16_to_cpu(*((__le16 *)skb->data));

	if (type & P54_HDR_FLAG_CONTROL)
		return p54_rx_control(dev, skb);
	else
		return p54_rx_data(dev, skb);
}
EXPORT_SYMBOL_GPL(p54_rx);

/*
 * So, the firmware is somewhat stupid and doesn't know what places in its
 * memory incoming data should go to. By poking around in the firmware, we
 * can find some unused memory to upload our packets to. However, data that we
 * want the card to TX needs to stay intact until the card has told us that
 * it is done with it. This function finds empty places we can upload to and
 * marks allocated areas as reserved if necessary. p54_rx_frame_sent frees
 * allocated areas.
 */
static int p54_assign_address(struct ieee80211_hw *dev, struct sk_buff *skb,
			       struct p54_hdr *data, u32 len)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *entry = priv->tx_queue.next;
	struct sk_buff *target_skb = NULL;
	struct ieee80211_tx_info *info;
	struct memrecord *range;
	u32 last_addr = priv->rx_start;
	u32 largest_hole = 0;
	u32 target_addr = priv->rx_start;
	unsigned long flags;
	unsigned int left;
	len = (len + priv->headroom + priv->tailroom + 3) & ~0x3;

	if (!skb)
		return -EINVAL;

	spin_lock_irqsave(&priv->tx_queue.lock, flags);

	left = skb_queue_len(&priv->tx_queue);
	if (unlikely(left >= 28)) {
		/*
		 * The tx_queue is nearly full!
		 * We have throttle normal data traffic, because we must
		 * have a few spare slots for control frames left.
		 */
		ieee80211_stop_queues(dev);
		queue_delayed_work(dev->workqueue, &priv->work,
				   msecs_to_jiffies(P54_TX_TIMEOUT));

		if (unlikely(left == 32)) {
			/*
			 * The tx_queue is now really full.
			 *
			 * TODO: check if the device has crashed and reset it.
			 */
			spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
			return -ENOSPC;
		}
	}

	while (left--) {
		u32 hole_size;
		info = IEEE80211_SKB_CB(entry);
		range = (void *)info->rate_driver_data;
		hole_size = range->start_addr - last_addr;
		if (!target_skb && hole_size >= len) {
			target_skb = entry->prev;
			hole_size -= len;
			target_addr = last_addr;
		}
		largest_hole = max(largest_hole, hole_size);
		last_addr = range->end_addr;
		entry = entry->next;
	}
	if (!target_skb && priv->rx_end - last_addr >= len) {
		target_skb = priv->tx_queue.prev;
		largest_hole = max(largest_hole, priv->rx_end - last_addr - len);
		if (!skb_queue_empty(&priv->tx_queue)) {
			info = IEEE80211_SKB_CB(target_skb);
			range = (void *)info->rate_driver_data;
			target_addr = range->end_addr;
		}
	} else
		largest_hole = max(largest_hole, priv->rx_end - last_addr);

	if (!target_skb) {
		spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
		ieee80211_stop_queues(dev);
		return -ENOSPC;
	}

	info = IEEE80211_SKB_CB(skb);
	range = (void *)info->rate_driver_data;
	range->start_addr = target_addr;
	range->end_addr = target_addr + len;
	__skb_queue_after(&priv->tx_queue, target_skb, skb);
	spin_unlock_irqrestore(&priv->tx_queue.lock, flags);

	if (largest_hole < priv->headroom + sizeof(struct p54_hdr) +
			   48 + IEEE80211_MAX_RTS_THRESHOLD + priv->tailroom)
		ieee80211_stop_queues(dev);

	data->req_id = cpu_to_le32(target_addr + priv->headroom);
	return 0;
}

static struct sk_buff *p54_alloc_skb(struct ieee80211_hw *dev,
		u16 hdr_flags, u16 len, u16 type, gfp_t memflags)
{
	struct p54_common *priv = dev->priv;
	struct p54_hdr *hdr;
	struct sk_buff *skb;

	skb = __dev_alloc_skb(len + priv->tx_hdr_len, memflags);
	if (!skb)
		return NULL;
	skb_reserve(skb, priv->tx_hdr_len);

	hdr = (struct p54_hdr *) skb_put(skb, sizeof(*hdr));
	hdr->flags = cpu_to_le16(hdr_flags);
	hdr->len = cpu_to_le16(len - sizeof(*hdr));
	hdr->type = cpu_to_le16(type);
	hdr->tries = hdr->rts_tries = 0;

	if (unlikely(p54_assign_address(dev, skb, hdr, len))) {
		kfree_skb(skb);
		return NULL;
	}
	return skb;
}

int p54_read_eeprom(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	struct p54_hdr *hdr = NULL;
	struct p54_eeprom_lm86 *eeprom_hdr;
	struct sk_buff *skb;
	size_t eeprom_size = 0x2020, offset = 0, blocksize, maxblocksize;
	int ret = -ENOMEM;
	void *eeprom = NULL;

	maxblocksize = EEPROM_READBACK_LEN;
	if (priv->fw_var >= 0x509)
		maxblocksize -= 0xc;
	else
		maxblocksize -= 0x4;

	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL, sizeof(*hdr) +
			    sizeof(*eeprom_hdr) + maxblocksize,
			    P54_CONTROL_TYPE_EEPROM_READBACK, GFP_KERNEL);
	if (!skb)
		goto free;
	priv->eeprom = kzalloc(EEPROM_READBACK_LEN, GFP_KERNEL);
	if (!priv->eeprom)
		goto free;
	eeprom = kzalloc(eeprom_size, GFP_KERNEL);
	if (!eeprom)
		goto free;

	eeprom_hdr = (struct p54_eeprom_lm86 *) skb_put(skb,
		     sizeof(*eeprom_hdr) + maxblocksize);

	while (eeprom_size) {
		blocksize = min(eeprom_size, maxblocksize);
		if (priv->fw_var < 0x509) {
			eeprom_hdr->v1.offset = cpu_to_le16(offset);
			eeprom_hdr->v1.len = cpu_to_le16(blocksize);
		} else {
			eeprom_hdr->v2.offset = cpu_to_le32(offset);
			eeprom_hdr->v2.len = cpu_to_le16(blocksize);
			eeprom_hdr->v2.magic2 = 0xf;
			memcpy(eeprom_hdr->v2.magic, (const char *)"LOCK", 4);
		}
		priv->tx(dev, skb);

		if (!wait_for_completion_interruptible_timeout(&priv->eeprom_comp, HZ)) {
			printk(KERN_ERR "%s: device does not respond!\n",
				wiphy_name(dev->wiphy));
			ret = -EBUSY;
			goto free;
	        }

		memcpy(eeprom + offset, priv->eeprom, blocksize);
		offset += blocksize;
		eeprom_size -= blocksize;
	}

	ret = p54_parse_eeprom(dev, eeprom, offset);
free:
	kfree(priv->eeprom);
	priv->eeprom = NULL;
	p54_free_skb(dev, skb);
	kfree(eeprom);

	return ret;
}
EXPORT_SYMBOL_GPL(p54_read_eeprom);

static int p54_set_tim(struct ieee80211_hw *dev, struct ieee80211_sta *sta,
		bool set)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;
	struct p54_tim *tim;

	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL_OPSET,
		      sizeof(struct p54_hdr) + sizeof(*tim),
		      P54_CONTROL_TYPE_TIM, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	tim = (struct p54_tim *) skb_put(skb, sizeof(*tim));
	tim->count = 1;
	tim->entry[0] = cpu_to_le16(set ? (sta->aid | 0x8000) : sta->aid);
	priv->tx(dev, skb);
	return 0;
}

static int p54_sta_unlock(struct ieee80211_hw *dev, u8 *addr)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;
	struct p54_sta_unlock *sta;

	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL_OPSET,
		sizeof(struct p54_hdr) + sizeof(*sta),
		P54_CONTROL_TYPE_PSM_STA_UNLOCK, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	sta = (struct p54_sta_unlock *)skb_put(skb, sizeof(*sta));
	memcpy(sta->addr, addr, ETH_ALEN);
	priv->tx(dev, skb);
	return 0;
}

static void p54_sta_notify(struct ieee80211_hw *dev, struct ieee80211_vif *vif,
			      enum sta_notify_cmd notify_cmd,
			      struct ieee80211_sta *sta)
{
	switch (notify_cmd) {
	case STA_NOTIFY_ADD:
	case STA_NOTIFY_REMOVE:
		/*
		 * Notify the firmware that we don't want or we don't
		 * need to buffer frames for this station anymore.
		 */

		p54_sta_unlock(dev, sta->addr);
		break;
	case STA_NOTIFY_AWAKE:
		/* update the firmware's filter table */
		p54_sta_unlock(dev, sta->addr);
		break;
	default:
		break;
	}
}

static int p54_tx_cancel(struct ieee80211_hw *dev, struct sk_buff *entry)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;
	struct p54_hdr *hdr;
	struct p54_txcancel *cancel;

	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL_OPSET,
		sizeof(struct p54_hdr) + sizeof(*cancel),
		P54_CONTROL_TYPE_TXCANCEL, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = (void *)entry->data;
	cancel = (struct p54_txcancel *)skb_put(skb, sizeof(*cancel));
	cancel->req_id = hdr->req_id;
	priv->tx(dev, skb);
	return 0;
}

static int p54_tx_fill(struct ieee80211_hw *dev, struct sk_buff *skb,
		struct ieee80211_tx_info *info, u8 *queue, size_t *extra_len,
		u16 *flags, u16 *aid)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct p54_common *priv = dev->priv;
	int ret = 0;

	if (unlikely(ieee80211_is_mgmt(hdr->frame_control))) {
		if (ieee80211_is_beacon(hdr->frame_control)) {
			*aid = 0;
			*queue = 0;
			*extra_len = IEEE80211_MAX_TIM_LEN;
			*flags = P54_HDR_FLAG_DATA_OUT_TIMESTAMP;
			return 0;
		} else if (ieee80211_is_probe_resp(hdr->frame_control)) {
			*aid = 0;
			*queue = 2;
			*flags = P54_HDR_FLAG_DATA_OUT_TIMESTAMP |
				 P54_HDR_FLAG_DATA_OUT_NOCANCEL;
			return 0;
		} else {
			*queue = 2;
			ret = 0;
		}
	} else {
		*queue += 4;
		ret = 1;
	}

	switch (priv->mode) {
	case NL80211_IFTYPE_STATION:
		*aid = 1;
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		if (info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM) {
			*aid = 0;
			*queue = 3;
			return 0;
		}
		if (info->control.sta)
			*aid = info->control.sta->aid;
		else
			*flags |= P54_HDR_FLAG_DATA_OUT_NOCANCEL;
	}
	return ret;
}

static u8 p54_convert_algo(enum ieee80211_key_alg alg)
{
	switch (alg) {
	case ALG_WEP:
		return P54_CRYPTO_WEP;
	case ALG_TKIP:
		return P54_CRYPTO_TKIPMICHAEL;
	case ALG_CCMP:
		return P54_CRYPTO_AESCCMP;
	default:
		return 0;
	}
}

static int p54_tx(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_queue_stats *current_queue = NULL;
	struct p54_common *priv = dev->priv;
	struct p54_hdr *hdr;
	struct p54_tx_data *txhdr;
	size_t padding, len, tim_len = 0;
	int i, j, ridx, ret;
	u16 hdr_flags = 0, aid = 0;
	u8 rate, queue, crypt_offset = 0;
	u8 cts_rate = 0x20;
	u8 rc_flags;
	u8 calculated_tries[4];
	u8 nrates = 0, nremaining = 8;

	queue = skb_get_queue_mapping(skb);

	ret = p54_tx_fill(dev, skb, info, &queue, &tim_len, &hdr_flags, &aid);
	current_queue = &priv->tx_stats[queue];
	if (unlikely((current_queue->len > current_queue->limit) && ret))
		return NETDEV_TX_BUSY;
	current_queue->len++;
	current_queue->count++;
	if ((current_queue->len == current_queue->limit) && ret)
		ieee80211_stop_queue(dev, skb_get_queue_mapping(skb));

	padding = (unsigned long)(skb->data - (sizeof(*hdr) + sizeof(*txhdr))) & 3;
	len = skb->len;

	if (info->control.hw_key) {
		crypt_offset = ieee80211_get_hdrlen_from_skb(skb);
		if (info->control.hw_key->alg == ALG_TKIP) {
			u8 *iv = (u8 *)(skb->data + crypt_offset);
			/*
			 * The firmware excepts that the IV has to have
			 * this special format
			 */
			iv[1] = iv[0];
			iv[0] = iv[2];
			iv[2] = 0;
		}
	}

	txhdr = (struct p54_tx_data *) skb_push(skb, sizeof(*txhdr) + padding);
	hdr = (struct p54_hdr *) skb_push(skb, sizeof(*hdr));

	if (padding)
		hdr_flags |= P54_HDR_FLAG_DATA_ALIGN;
	hdr->type = cpu_to_le16(aid);
	hdr->rts_tries = info->control.rates[0].count;

	/*
	 * we register the rates in perfect order, and
	 * RTS/CTS won't happen on 5 GHz
	 */
	cts_rate = info->control.rts_cts_rate_idx;

	memset(&txhdr->rateset, 0, sizeof(txhdr->rateset));

	/* see how many rates got used */
	for (i = 0; i < 4; i++) {
		if (info->control.rates[i].idx < 0)
			break;
		nrates++;
	}

	/* limit tries to 8/nrates per rate */
	for (i = 0; i < nrates; i++) {
		/*
		 * The magic expression here is equivalent to 8/nrates for
		 * all values that matter, but avoids division and jumps.
		 * Note that nrates can only take the values 1 through 4.
		 */
		calculated_tries[i] = min_t(int, ((15 >> nrates) | 1) + 1,
						 info->control.rates[i].count);
		nremaining -= calculated_tries[i];
	}

	/* if there are tries left, distribute from back to front */
	for (i = nrates - 1; nremaining > 0 && i >= 0; i--) {
		int tmp = info->control.rates[i].count - calculated_tries[i];

		if (tmp <= 0)
			continue;
		/* RC requested more tries at this rate */

		tmp = min_t(int, tmp, nremaining);
		calculated_tries[i] += tmp;
		nremaining -= tmp;
	}

	ridx = 0;
	for (i = 0; i < nrates && ridx < 8; i++) {
		/* we register the rates in perfect order */
		rate = info->control.rates[i].idx;
		if (info->band == IEEE80211_BAND_5GHZ)
			rate += 4;

		/* store the count we actually calculated for TX status */
		info->control.rates[i].count = calculated_tries[i];

		rc_flags = info->control.rates[i].flags;
		if (rc_flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE) {
			rate |= 0x10;
			cts_rate |= 0x10;
		}
		if (rc_flags & IEEE80211_TX_RC_USE_RTS_CTS)
			rate |= 0x40;
		else if (rc_flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
			rate |= 0x20;
		for (j = 0; j < calculated_tries[i] && ridx < 8; j++) {
			txhdr->rateset[ridx] = rate;
			ridx++;
		}
	}

	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ)
		hdr_flags |= P54_HDR_FLAG_DATA_OUT_SEQNR;

	/* TODO: enable bursting */
	hdr->flags = cpu_to_le16(hdr_flags);
	hdr->tries = ridx;
	txhdr->rts_rate_idx = 0;
	if (info->control.hw_key) {
		txhdr->key_type = p54_convert_algo(info->control.hw_key->alg);
		txhdr->key_len = min((u8)16, info->control.hw_key->keylen);
		memcpy(txhdr->key, info->control.hw_key->key, txhdr->key_len);
		if (info->control.hw_key->alg == ALG_TKIP) {
			if (unlikely(skb_tailroom(skb) < 12))
				goto err;
			/* reserve space for the MIC key */
			len += 8;
			memcpy(skb_put(skb, 8), &(info->control.hw_key->key
				[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY]), 8);
		}
		/* reserve some space for ICV */
		len += info->control.hw_key->icv_len;
		memset(skb_put(skb, info->control.hw_key->icv_len), 0,
		       info->control.hw_key->icv_len);
	} else {
		txhdr->key_type = 0;
		txhdr->key_len = 0;
	}
	txhdr->crypt_offset = crypt_offset;
	txhdr->hw_queue = queue;
	if (current_queue)
		txhdr->backlog = current_queue->len;
	else
		txhdr->backlog = 0;
	memset(txhdr->durations, 0, sizeof(txhdr->durations));
	txhdr->tx_antenna = (info->antenna_sel_tx == 0) ?
		2 : info->antenna_sel_tx - 1;
	txhdr->output_power = priv->output_power;
	txhdr->cts_rate = cts_rate;
	if (padding)
		txhdr->align[0] = padding;

	hdr->len = cpu_to_le16(len);
	/* modifies skb->cb and with it info, so must be last! */
	if (unlikely(p54_assign_address(dev, skb, hdr, skb->len + tim_len)))
		goto err;
	priv->tx(dev, skb);

	queue_delayed_work(dev->workqueue, &priv->work,
			   msecs_to_jiffies(P54_TX_FRAME_LIFETIME));

	return 0;

 err:
	skb_pull(skb, sizeof(*hdr) + sizeof(*txhdr) + padding);
	if (current_queue) {
		current_queue->len--;
		current_queue->count--;
	}
	return NETDEV_TX_BUSY;
}

static int p54_setup_mac(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;
	struct p54_setup_mac *setup;
	u16 mode;

	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*setup) +
			    sizeof(struct p54_hdr), P54_CONTROL_TYPE_SETUP,
			    GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	setup = (struct p54_setup_mac *) skb_put(skb, sizeof(*setup));
	if (dev->conf.radio_enabled) {
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
		default:
			mode = P54_FILTER_TYPE_NONE;
			break;
		}
		if (priv->filter_flags & FIF_PROMISC_IN_BSS)
			mode |= P54_FILTER_TYPE_TRANSPARENT;
	} else
		mode = P54_FILTER_TYPE_RX_DISABLED;

	setup->mac_mode = cpu_to_le16(mode);
	memcpy(setup->mac_addr, priv->mac_addr, ETH_ALEN);
	memcpy(setup->bssid, priv->bssid, ETH_ALEN);
	setup->rx_antenna = 2; /* automatic */
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
	priv->tx(dev, skb);
	return 0;
}

static int p54_scan(struct ieee80211_hw *dev, u16 mode, u16 dwell)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;
	struct p54_scan *chan;
	unsigned int i;
	void *entry;
	__le16 freq = cpu_to_le16(dev->conf.channel->center_freq);
	int band = dev->conf.channel->band;

	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*chan) +
			    sizeof(struct p54_hdr), P54_CONTROL_TYPE_SCAN,
			    GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	chan = (struct p54_scan *) skb_put(skb, sizeof(*chan));
	memset(chan->padding1, 0, sizeof(chan->padding1));
	chan->mode = cpu_to_le16(mode);
	chan->dwell = cpu_to_le16(dwell);

	for (i = 0; i < priv->iq_autocal_len; i++) {
		if (priv->iq_autocal[i].freq != freq)
			continue;

		memcpy(&chan->iq_autocal, &priv->iq_autocal[i],
		       sizeof(*priv->iq_autocal));
		break;
	}
	if (i == priv->iq_autocal_len)
		goto err;

	for (i = 0; i < priv->output_limit_len; i++) {
		if (priv->output_limit[i].freq != freq)
			continue;

		chan->val_barker = 0x38;
		chan->val_bpsk = chan->dup_bpsk =
			priv->output_limit[i].val_bpsk;
		chan->val_qpsk = chan->dup_qpsk =
			priv->output_limit[i].val_qpsk;
		chan->val_16qam = chan->dup_16qam =
			priv->output_limit[i].val_16qam;
		chan->val_64qam = chan->dup_64qam =
			priv->output_limit[i].val_64qam;
		break;
	}
	if (i == priv->output_limit_len)
		goto err;

	entry = priv->curve_data->data;
	for (i = 0; i < priv->curve_data->channels; i++) {
		if (*((__le16 *)entry) != freq) {
			entry += sizeof(__le16);
			entry += sizeof(struct p54_pa_curve_data_sample) *
				 priv->curve_data->points_per_channel;
			continue;
		}

		entry += sizeof(__le16);
		chan->pa_points_per_curve = 8;
		memset(chan->curve_data, 0, sizeof(*chan->curve_data));
		memcpy(chan->curve_data, entry,
		       sizeof(struct p54_pa_curve_data_sample) *
		       min((u8)8, priv->curve_data->points_per_channel));
		break;
	}

	if (priv->fw_var < 0x500) {
		chan->v1_rssi.mul = cpu_to_le16(priv->rssical_db[band].mul);
		chan->v1_rssi.add = cpu_to_le16(priv->rssical_db[band].add);
	} else {
		chan->v2.rssi.mul = cpu_to_le16(priv->rssical_db[band].mul);
		chan->v2.rssi.add = cpu_to_le16(priv->rssical_db[band].add);
		chan->v2.basic_rate_mask = cpu_to_le32(priv->basic_rate_mask);
		memset(chan->v2.rts_rates, 0, 8);
	}
	priv->tx(dev, skb);
	return 0;

 err:
	printk(KERN_ERR "%s: frequency change failed\n", wiphy_name(dev->wiphy));
	p54_free_skb(dev, skb);
	return -EINVAL;
}

static int p54_set_leds(struct ieee80211_hw *dev, int mode, int link, int act)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;
	struct p54_led *led;

	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*led) +
			sizeof(struct p54_hdr),	P54_CONTROL_TYPE_LED,
			GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	led = (struct p54_led *)skb_put(skb, sizeof(*led));
	led->mode = cpu_to_le16(mode);
	led->led_permanent = cpu_to_le16(link);
	led->led_temporary = cpu_to_le16(act);
	led->duration = cpu_to_le16(1000);
	priv->tx(dev, skb);
	return 0;
}

#define P54_SET_QUEUE(queue, ai_fs, cw_min, cw_max, _txop)	\
do {	 							\
	queue.aifs = cpu_to_le16(ai_fs);			\
	queue.cwmin = cpu_to_le16(cw_min);			\
	queue.cwmax = cpu_to_le16(cw_max);			\
	queue.txop = cpu_to_le16(_txop);			\
} while(0)

static int p54_set_edcf(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;
	struct p54_edcf *edcf;

	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*edcf) +
			sizeof(struct p54_hdr), P54_CONTROL_TYPE_DCFINIT,
			GFP_ATOMIC);
	if (!skb)
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
	priv->tx(dev, skb);
	return 0;
}

static int p54_beacon_tim(struct sk_buff *skb)
{
	/*
	 * the good excuse for this mess is ... the firmware.
	 * The dummy TIM MUST be at the end of the beacon frame,
	 * because it'll be overwritten!
	 */

	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	u8 *pos, *end;

	if (skb->len <= sizeof(mgmt))
		return -EINVAL;

	pos = (u8 *)mgmt->u.beacon.variable;
	end = skb->data + skb->len;
	while (pos < end) {
		if (pos + 2 + pos[1] > end)
			return -EINVAL;

		if (pos[0] == WLAN_EID_TIM) {
			u8 dtim_len = pos[1];
			u8 dtim_period = pos[3];
			u8 *next = pos + 2 + dtim_len;

			if (dtim_len < 3)
				return -EINVAL;

			memmove(pos, next, end - next);

			if (dtim_len > 3)
				skb_trim(skb, skb->len - (dtim_len - 3));

			pos = end - (dtim_len + 2);

			/* add the dummy at the end */
			pos[0] = WLAN_EID_TIM;
			pos[1] = 3;
			pos[2] = 0;
			pos[3] = dtim_period;
			pos[4] = 0;
			return 0;
		}
		pos += 2 + pos[1];
	}
	return 0;
}

static int p54_beacon_update(struct ieee80211_hw *dev,
			struct ieee80211_vif *vif)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *beacon;
	int ret;

	if (priv->cached_beacon) {
		p54_tx_cancel(dev, priv->cached_beacon);
		/* wait for the last beacon the be freed */
		msleep(10);
	}

	beacon = ieee80211_beacon_get(dev, vif);
	if (!beacon)
		return -ENOMEM;
	ret = p54_beacon_tim(beacon);
	if (ret)
		return ret;
	ret = p54_tx(dev, beacon);
	if (ret)
		return ret;
	priv->cached_beacon = beacon;
	priv->tsf_high32 = 0;
	priv->tsf_low32 = 0;

	return 0;
}

static int p54_start(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	int err;

	mutex_lock(&priv->conf_mutex);
	err = priv->open(dev);
	if (err)
		goto out;
	P54_SET_QUEUE(priv->qos_params[0], 0x0002, 0x0003, 0x0007, 47);
	P54_SET_QUEUE(priv->qos_params[1], 0x0002, 0x0007, 0x000f, 94);
	P54_SET_QUEUE(priv->qos_params[2], 0x0003, 0x000f, 0x03ff, 0);
	P54_SET_QUEUE(priv->qos_params[3], 0x0007, 0x000f, 0x03ff, 0);
	err = p54_set_edcf(dev);
	if (err)
		goto out;

	memset(priv->bssid, ~0, ETH_ALEN);
	priv->mode = NL80211_IFTYPE_MONITOR;
	err = p54_setup_mac(dev);
	if (err) {
		priv->mode = NL80211_IFTYPE_UNSPECIFIED;
		goto out;
	}

	queue_delayed_work(dev->workqueue, &priv->work, 0);

out:
	mutex_unlock(&priv->conf_mutex);
	return err;
}

static void p54_stop(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;

	mutex_lock(&priv->conf_mutex);
	priv->mode = NL80211_IFTYPE_UNSPECIFIED;
	cancel_delayed_work_sync(&priv->work);
	if (priv->cached_beacon)
		p54_tx_cancel(dev, priv->cached_beacon);

	priv->stop(dev);
	while ((skb = skb_dequeue(&priv->tx_queue)))
		kfree_skb(skb);
	priv->cached_beacon = NULL;
	priv->tsf_high32 = priv->tsf_low32 = 0;
	mutex_unlock(&priv->conf_mutex);
}

static int p54_add_interface(struct ieee80211_hw *dev,
			     struct ieee80211_if_init_conf *conf)
{
	struct p54_common *priv = dev->priv;

	mutex_lock(&priv->conf_mutex);
	if (priv->mode != NL80211_IFTYPE_MONITOR) {
		mutex_unlock(&priv->conf_mutex);
		return -EOPNOTSUPP;
	}

	switch (conf->type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		priv->mode = conf->type;
		break;
	default:
		mutex_unlock(&priv->conf_mutex);
		return -EOPNOTSUPP;
	}

	memcpy(priv->mac_addr, conf->mac_addr, ETH_ALEN);
	p54_setup_mac(dev);
	p54_set_leds(dev, 1, 0, 0);
	mutex_unlock(&priv->conf_mutex);
	return 0;
}

static void p54_remove_interface(struct ieee80211_hw *dev,
				 struct ieee80211_if_init_conf *conf)
{
	struct p54_common *priv = dev->priv;

	mutex_lock(&priv->conf_mutex);
	if (priv->cached_beacon)
		p54_tx_cancel(dev, priv->cached_beacon);
	priv->mode = NL80211_IFTYPE_MONITOR;
	memset(priv->mac_addr, 0, ETH_ALEN);
	memset(priv->bssid, 0, ETH_ALEN);
	p54_setup_mac(dev);
	mutex_unlock(&priv->conf_mutex);
}

static int p54_config(struct ieee80211_hw *dev, u32 changed)
{
	int ret = 0;
	struct p54_common *priv = dev->priv;
	struct ieee80211_conf *conf = &dev->conf;

	mutex_lock(&priv->conf_mutex);
	if (changed & IEEE80211_CONF_CHANGE_POWER)
		priv->output_power = conf->power_level << 2;
	if (changed & IEEE80211_CONF_CHANGE_RADIO_ENABLED) {
		ret = p54_setup_mac(dev);
		if (ret)
			goto out;
	}
	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ret = p54_scan(dev, P54_SCAN_EXIT, 0);
		if (ret)
			goto out;
	}

out:
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

static int p54_config_interface(struct ieee80211_hw *dev,
				struct ieee80211_vif *vif,
				struct ieee80211_if_conf *conf)
{
	struct p54_common *priv = dev->priv;
	int ret = 0;

	mutex_lock(&priv->conf_mutex);
	if (conf->changed & IEEE80211_IFCC_BSSID) {
		memcpy(priv->bssid, conf->bssid, ETH_ALEN);
		ret = p54_setup_mac(dev);
		if (ret)
			goto out;
	}

	if (conf->changed & IEEE80211_IFCC_BEACON) {
		ret = p54_scan(dev, P54_SCAN_EXIT, 0);
		if (ret)
			goto out;
		ret = p54_setup_mac(dev);
		if (ret)
			goto out;
		ret = p54_beacon_update(dev, vif);
		if (ret)
			goto out;
		ret = p54_set_edcf(dev);
		if (ret)
			goto out;
	}

	ret = p54_set_leds(dev, 1, !is_multicast_ether_addr(priv->bssid), 0);

out:
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

static void p54_configure_filter(struct ieee80211_hw *dev,
				 unsigned int changed_flags,
				 unsigned int *total_flags,
				 int mc_count, struct dev_mc_list *mclist)
{
	struct p54_common *priv = dev->priv;

	*total_flags &= FIF_PROMISC_IN_BSS |
			(*total_flags & FIF_PROMISC_IN_BSS) ?
				FIF_FCSFAIL : 0;

	priv->filter_flags = *total_flags;

	if (changed_flags & FIF_PROMISC_IN_BSS)
		p54_setup_mac(dev);
}

static int p54_conf_tx(struct ieee80211_hw *dev, u16 queue,
		       const struct ieee80211_tx_queue_params *params)
{
	struct p54_common *priv = dev->priv;
	int ret;

	mutex_lock(&priv->conf_mutex);
	if ((params) && !(queue > 4)) {
		P54_SET_QUEUE(priv->qos_params[queue], params->aifs,
			params->cw_min, params->cw_max, params->txop);
		ret = p54_set_edcf(dev);
	} else
		ret = -EINVAL;
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

static int p54_init_xbow_synth(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;
	struct p54_xbow_synth *xbow;

	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*xbow) +
			    sizeof(struct p54_hdr),
			    P54_CONTROL_TYPE_XBOW_SYNTH_CFG,
			    GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	xbow = (struct p54_xbow_synth *)skb_put(skb, sizeof(*xbow));
	xbow->magic1 = cpu_to_le16(0x1);
	xbow->magic2 = cpu_to_le16(0x2);
	xbow->freq = cpu_to_le16(5390);
	memset(xbow->padding, 0, sizeof(xbow->padding));
	priv->tx(dev, skb);
	return 0;
}

static void p54_work(struct work_struct *work)
{
	struct p54_common *priv = container_of(work, struct p54_common,
					       work.work);
	struct ieee80211_hw *dev = priv->hw;
	struct sk_buff *skb;

	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED))
		return ;

	/*
	 * TODO: walk through tx_queue and do the following tasks
	 * 	1. initiate bursts.
	 *      2. cancel stuck frames / reset the device if necessary.
	 */

	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL, sizeof(struct p54_hdr) +
			    sizeof(struct p54_statistics),
			    P54_CONTROL_TYPE_STAT_READBACK, GFP_KERNEL);
	if (!skb)
		return ;

	priv->tx(dev, skb);
}

static int p54_get_stats(struct ieee80211_hw *dev,
			 struct ieee80211_low_level_stats *stats)
{
	struct p54_common *priv = dev->priv;

	memcpy(stats, &priv->stats, sizeof(*stats));
	return 0;
}

static int p54_get_tx_stats(struct ieee80211_hw *dev,
			    struct ieee80211_tx_queue_stats *stats)
{
	struct p54_common *priv = dev->priv;

	memcpy(stats, &priv->tx_stats[4], sizeof(stats[0]) * dev->queues);

	return 0;
}

static void p54_bss_info_changed(struct ieee80211_hw *dev,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *info,
				 u32 changed)
{
	struct p54_common *priv = dev->priv;

	if (changed & BSS_CHANGED_ERP_SLOT) {
		priv->use_short_slot = info->use_short_slot;
		p54_set_edcf(dev);
	}
	if (changed & BSS_CHANGED_BASIC_RATES) {
		if (dev->conf.channel->band == IEEE80211_BAND_5GHZ)
			priv->basic_rate_mask = (info->basic_rates << 4);
		else
			priv->basic_rate_mask = info->basic_rates;
		p54_setup_mac(dev);
		if (priv->fw_var >= 0x500)
			p54_scan(dev, P54_SCAN_EXIT, 0);
	}
	if (changed & BSS_CHANGED_ASSOC) {
		if (info->assoc) {
			priv->aid = info->aid;
			priv->wakeup_timer = info->beacon_int *
					     info->dtim_period * 5;
			p54_setup_mac(dev);
		}
	}

}

static int p54_set_key(struct ieee80211_hw *dev, enum set_key_cmd cmd,
		       const u8 *local_address, const u8 *address,
		       struct ieee80211_key_conf *key)
{
	struct p54_common *priv = dev->priv;
	struct sk_buff *skb;
	struct p54_keycache *rxkey;
	u8 algo = 0;

	if (modparam_nohwcrypt)
		return -EOPNOTSUPP;

	if (cmd == DISABLE_KEY)
		algo = 0;
	else {
		switch (key->alg) {
		case ALG_TKIP:
			if (!(priv->privacy_caps & (BR_DESC_PRIV_CAP_MICHAEL |
			      BR_DESC_PRIV_CAP_TKIP)))
				return -EOPNOTSUPP;
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			algo = P54_CRYPTO_TKIPMICHAEL;
			break;
		case ALG_WEP:
			if (!(priv->privacy_caps & BR_DESC_PRIV_CAP_WEP))
				return -EOPNOTSUPP;
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			algo = P54_CRYPTO_WEP;
			break;
		case ALG_CCMP:
			if (!(priv->privacy_caps & BR_DESC_PRIV_CAP_AESCCMP))
				return -EOPNOTSUPP;
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			algo = P54_CRYPTO_AESCCMP;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	if (key->keyidx > priv->rx_keycache_size) {
		/*
		 * The device supports the choosen algorithm, but the firmware
		 * does not provide enough key slots to store all of them.
		 * So, incoming frames have to be decoded by the mac80211 stack,
		 * but we can still offload encryption for outgoing frames.
		 */

		return 0;
	}

	mutex_lock(&priv->conf_mutex);
	skb = p54_alloc_skb(dev, P54_HDR_FLAG_CONTROL_OPSET, sizeof(*rxkey) +
			sizeof(struct p54_hdr),	P54_CONTROL_TYPE_RX_KEYCACHE,
			GFP_ATOMIC);
	if (!skb) {
		mutex_unlock(&priv->conf_mutex);
		return -ENOMEM;
	}

	/* TODO: some devices have 4 more free slots for rx keys */
	rxkey = (struct p54_keycache *)skb_put(skb, sizeof(*rxkey));
	rxkey->entry = key->keyidx;
	rxkey->key_id = key->keyidx;
	rxkey->key_type = algo;
	if (address)
		memcpy(rxkey->mac, address, ETH_ALEN);
	else
		memset(rxkey->mac, ~0, ETH_ALEN);
	if (key->alg != ALG_TKIP) {
		rxkey->key_len = min((u8)16, key->keylen);
		memcpy(rxkey->key, key->key, rxkey->key_len);
	} else {
		rxkey->key_len = 24;
		memcpy(rxkey->key, key->key, 16);
		memcpy(&(rxkey->key[16]), &(key->key
			[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY]), 8);
	}

	priv->tx(dev, skb);
	mutex_unlock(&priv->conf_mutex);
	return 0;
}

static const struct ieee80211_ops p54_ops = {
	.tx			= p54_tx,
	.start			= p54_start,
	.stop			= p54_stop,
	.add_interface		= p54_add_interface,
	.remove_interface	= p54_remove_interface,
	.set_tim		= p54_set_tim,
	.sta_notify		= p54_sta_notify,
	.set_key		= p54_set_key,
	.config			= p54_config,
	.config_interface	= p54_config_interface,
	.bss_info_changed	= p54_bss_info_changed,
	.configure_filter	= p54_configure_filter,
	.conf_tx		= p54_conf_tx,
	.get_stats		= p54_get_stats,
	.get_tx_stats		= p54_get_tx_stats
};

struct ieee80211_hw *p54_init_common(size_t priv_data_len)
{
	struct ieee80211_hw *dev;
	struct p54_common *priv;

	dev = ieee80211_alloc_hw(priv_data_len, &p54_ops);
	if (!dev)
		return NULL;

	priv = dev->priv;
	priv->hw = dev;
	priv->mode = NL80211_IFTYPE_UNSPECIFIED;
	priv->basic_rate_mask = 0x15f;
	skb_queue_head_init(&priv->tx_queue);
	dev->flags = IEEE80211_HW_RX_INCLUDES_FCS |
		     IEEE80211_HW_SIGNAL_DBM |
		     IEEE80211_HW_NOISE_DBM;

	dev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				      BIT(NL80211_IFTYPE_ADHOC) |
				      BIT(NL80211_IFTYPE_AP) |
				      BIT(NL80211_IFTYPE_MESH_POINT);

	dev->channel_change_time = 1000;	/* TODO: find actual value */
	priv->tx_stats[0].limit = 1;		/* Beacon queue */
	priv->tx_stats[1].limit = 1;		/* Probe queue for HW scan */
	priv->tx_stats[2].limit = 3;		/* queue for MLMEs */
	priv->tx_stats[3].limit = 3;		/* Broadcast / MC queue */
	priv->tx_stats[4].limit = 5;		/* Data */
	dev->queues = 1;
	priv->noise = -94;
	/*
	 * We support at most 8 tries no matter which rate they're at,
	 * we cannot support max_rates * max_rate_tries as we set it
	 * here, but setting it correctly to 4/2 or so would limit us
	 * artificially if the RC algorithm wants just two rates, so
	 * let's say 4/7, we'll redistribute it at TX time, see the
	 * comments there.
	 */
	dev->max_rates = 4;
	dev->max_rate_tries = 7;
	dev->extra_tx_headroom = sizeof(struct p54_hdr) + 4 +
				 sizeof(struct p54_tx_data);

	mutex_init(&priv->conf_mutex);
	init_completion(&priv->eeprom_comp);
	INIT_DELAYED_WORK(&priv->work, p54_work);

	return dev;
}
EXPORT_SYMBOL_GPL(p54_init_common);

void p54_free_common(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	kfree(priv->iq_autocal);
	kfree(priv->output_limit);
	kfree(priv->curve_data);
}
EXPORT_SYMBOL_GPL(p54_free_common);

static int __init p54_init(void)
{
	return 0;
}

static void __exit p54_exit(void)
{
}

module_init(p54_init);
module_exit(p54_exit);
