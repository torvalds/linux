/*
 * EEPROM parser code for mac80211 Prism54 drivers
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
#include <linux/firmware.h>
#include <linux/etherdevice.h>

#include <net/mac80211.h>

#include "p54.h"
#include "eeprom.h"
#include "lmac.h"

static struct ieee80211_rate p54_bgrates[] = {
	{ .bitrate = 10, .hw_value = 0, },
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

	priv->curve_data = kmalloc(sizeof(*priv->curve_data) + cd_len,
				   GFP_KERNEL);
	if (!priv->curve_data)
		return -ENOMEM;

	priv->curve_data->entries = curve_data->channels;
	priv->curve_data->entry_size = sizeof(__le16) +
		sizeof(*dst) * curve_data->points_per_channel;
	priv->curve_data->offset = offsetof(struct pda_pa_curve_data, data);
	priv->curve_data->len = cd_len;
	memcpy(priv->curve_data->data, curve_data, sizeof(*curve_data));
	source = curve_data->data;
	target = ((struct pda_pa_curve_data *) priv->curve_data->data)->data;
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
#define SUB(x, y) (u8)(((x) - (y)) > (x) ? 0 : (x) - (y))
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

	priv->curve_data = kzalloc(cd_len + sizeof(*priv->curve_data),
				   GFP_KERNEL);
	if (!priv->curve_data)
		return -ENOMEM;

	priv->curve_data->entries = curve_data->channels;
	priv->curve_data->entry_size = sizeof(__le16) +
		sizeof(*dst) * curve_data->points_per_channel;
	priv->curve_data->offset = offsetof(struct pda_pa_curve_data, data);
	priv->curve_data->len = cd_len;
	memcpy(priv->curve_data->data, curve_data, sizeof(*curve_data));
	source = curve_data->data;
	target = ((struct pda_pa_curve_data *) priv->curve_data->data)->data;
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

static const char *p54_rf_chips[] = { "INVALID-0", "Duette3", "Duette2",
	"Frisbee", "Xbow", "Longbow", "INVALID-6", "INVALID-7" };

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

static void p54_parse_default_country(struct ieee80211_hw *dev,
				      void *data, int len)
{
	struct pda_country *country;

	if (len != sizeof(*country)) {
		printk(KERN_ERR "%s: found possible invalid default country "
				"eeprom entry. (entry size: %d)\n",
		       wiphy_name(dev->wiphy), len);

		print_hex_dump_bytes("country:", DUMP_PREFIX_NONE,
				     data, len);

		printk(KERN_ERR "%s: please report this issue.\n",
			wiphy_name(dev->wiphy));
		return;
	}

	country = (struct pda_country *) data;
	if (country->flags == PDR_COUNTRY_CERT_CODE_PSEUDO)
		regulatory_hint(dev->wiphy, country->alpha2);
	else {
		/* TODO:
		 * write a shared/common function that converts
		 * "Regulatory domain codes" (802.11-2007 14.8.2.2)
		 * into ISO/IEC 3166-1 alpha2 for regulatory_hint.
		 */
	}
}

static int p54_convert_output_limits(struct ieee80211_hw *dev,
				     u8 *data, size_t len)
{
	struct p54_common *priv = dev->priv;

	if (len < 2)
		return -EINVAL;

	if (data[0] != 0) {
		printk(KERN_ERR "%s: unknown output power db revision:%x\n",
		       wiphy_name(dev->wiphy), data[0]);
		return -EINVAL;
	}

	if (2 + data[1] * sizeof(struct pda_channel_output_limit) > len)
		return -EINVAL;

	priv->output_limit = kmalloc(data[1] *
		sizeof(struct pda_channel_output_limit) +
		sizeof(*priv->output_limit), GFP_KERNEL);

	if (!priv->output_limit)
		return -ENOMEM;

	priv->output_limit->offset = 0;
	priv->output_limit->entries = data[1];
	priv->output_limit->entry_size =
		sizeof(struct pda_channel_output_limit);
	priv->output_limit->len = priv->output_limit->entry_size *
				  priv->output_limit->entries +
				  priv->output_limit->offset;

	memcpy(priv->output_limit->data, &data[2],
	       data[1] * sizeof(struct pda_channel_output_limit));

	return 0;
}

static struct p54_cal_database *p54_convert_db(struct pda_custom_wrapper *src,
					       size_t total_len)
{
	struct p54_cal_database *dst;
	size_t payload_len, entries, entry_size, offset;

	payload_len = le16_to_cpu(src->len);
	entries = le16_to_cpu(src->entries);
	entry_size = le16_to_cpu(src->entry_size);
	offset = le16_to_cpu(src->offset);
	if (((entries * entry_size + offset) != payload_len) ||
	     (payload_len + sizeof(*src) != total_len))
		return NULL;

	dst = kmalloc(sizeof(*dst) + payload_len, GFP_KERNEL);
	if (!dst)
		return NULL;

	dst->entries = entries;
	dst->entry_size = entry_size;
	dst->offset = offset;
	dst->len = payload_len;

	memcpy(dst->data, src->data, payload_len);
	return dst;
}

int p54_parse_eeprom(struct ieee80211_hw *dev, void *eeprom, int len)
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
			if (data_len != ETH_ALEN)
				break;
			SET_IEEE80211_PERM_ADDR(dev, entry->data);
			break;
		case PDR_PRISM_PA_CAL_OUTPUT_POWER_LIMITS:
			if (priv->output_limit)
				break;
			err = p54_convert_output_limits(dev, entry->data,
							data_len);
			if (err)
				goto err;
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
		case PDR_DEFAULT_COUNTRY:
			p54_parse_default_country(dev, entry->data, data_len);
			break;
		case PDR_INTERFACE_LIST:
			tmp = entry->data;
			while ((u8 *)tmp < entry->data + data_len) {
				struct exp_if *exp_if = tmp;
				if (exp_if->if_id == cpu_to_le16(IF_ID_ISL39000))
					synth = le16_to_cpu(exp_if->variant);
				tmp += sizeof(*exp_if);
			}
			break;
		case PDR_HARDWARE_PLATFORM_COMPONENT_ID:
			if (data_len < 2)
				break;
			priv->version = *(u8 *)(entry->data + 1);
			break;
		case PDR_RSSI_LINEAR_APPROXIMATION:
		case PDR_RSSI_LINEAR_APPROXIMATION_DUAL_BAND:
		case PDR_RSSI_LINEAR_APPROXIMATION_EXTENDED:
			p54_parse_rssical(dev, entry->data, data_len,
					  le16_to_cpu(entry->code));
			break;
		case PDR_RSSI_LINEAR_APPROXIMATION_CUSTOM: {
			__le16 *src = (void *) entry->data;
			s16 *dst = (void *) &priv->rssical_db;
			int i;

			if (data_len != sizeof(priv->rssical_db)) {
				err = -EINVAL;
				goto err;
			}
			for (i = 0; i < sizeof(priv->rssical_db) /
					sizeof(*src); i++)
				*(dst++) = (s16) le16_to_cpu(*(src++));
			}
			break;
		case PDR_PRISM_PA_CAL_OUTPUT_POWER_LIMITS_CUSTOM: {
			struct pda_custom_wrapper *pda = (void *) entry->data;
			if (priv->output_limit || data_len < sizeof(*pda))
				break;
			priv->output_limit = p54_convert_db(pda, data_len);
			}
			break;
		case PDR_PRISM_PA_CAL_CURVE_DATA_CUSTOM: {
			struct pda_custom_wrapper *pda = (void *) entry->data;
			if (priv->curve_data || data_len < sizeof(*pda))
				break;
			priv->curve_data = p54_convert_db(pda, data_len);
			}
			break;
		case PDR_END:
			/* make it overrun */
			entry_len = len;
			break;
		default:
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
	if (priv->rxhw == PDR_SYNTH_FRONTEND_XBOW)
		p54_init_xbow_synth(priv);
	if (!(synth & PDR_SYNTH_24_GHZ_DISABLED))
		dev->wiphy->bands[IEEE80211_BAND_2GHZ] = &band_2GHz;
	if (!(synth & PDR_SYNTH_5_GHZ_DISABLED))
		dev->wiphy->bands[IEEE80211_BAND_5GHZ] = &band_5GHz;
	if ((synth & PDR_SYNTH_RX_DIV_MASK) == PDR_SYNTH_RX_DIV_SUPPORTED)
		priv->rx_diversity_mask = 3;
	if ((synth & PDR_SYNTH_TX_DIV_MASK) == PDR_SYNTH_TX_DIV_SUPPORTED)
		priv->tx_diversity_mask = 3;

	if (!is_valid_ether_addr(dev->wiphy->perm_addr)) {
		u8 perm_addr[ETH_ALEN];

		printk(KERN_WARNING "%s: Invalid hwaddr! Using randomly generated MAC addr\n",
			wiphy_name(dev->wiphy));
		random_ether_addr(perm_addr);
		SET_IEEE80211_PERM_ADDR(dev, perm_addr);
	}

	printk(KERN_INFO "%s: hwaddr %pM, MAC:isl38%02x RF:%s\n",
		wiphy_name(dev->wiphy),	dev->wiphy->perm_addr, priv->version,
		p54_rf_chips[priv->rxhw]);

	return 0;

err:
	kfree(priv->iq_autocal);
	kfree(priv->output_limit);
	kfree(priv->curve_data);
	priv->iq_autocal = NULL;
	priv->output_limit = NULL;
	priv->curve_data = NULL;

	printk(KERN_ERR "%s: eeprom parse failed!\n",
		wiphy_name(dev->wiphy));
	return err;
}
EXPORT_SYMBOL_GPL(p54_parse_eeprom);

int p54_read_eeprom(struct ieee80211_hw *dev)
{
	struct p54_common *priv = dev->priv;
	size_t eeprom_size = 0x2020, offset = 0, blocksize, maxblocksize;
	int ret = -ENOMEM;
	void *eeprom = NULL;

	maxblocksize = EEPROM_READBACK_LEN;
	if (priv->fw_var >= 0x509)
		maxblocksize -= 0xc;
	else
		maxblocksize -= 0x4;

	eeprom = kzalloc(eeprom_size, GFP_KERNEL);
	if (unlikely(!eeprom))
		goto free;

	while (eeprom_size) {
		blocksize = min(eeprom_size, maxblocksize);
		ret = p54_download_eeprom(priv, (void *) (eeprom + offset),
					  offset, blocksize);
		if (unlikely(ret))
			goto free;

		offset += blocksize;
		eeprom_size -= blocksize;
	}

	ret = p54_parse_eeprom(dev, eeprom, offset);
free:
	kfree(eeprom);
	return ret;
}
EXPORT_SYMBOL_GPL(p54_read_eeprom);
