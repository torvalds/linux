// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/nvmem-consumer.h>
#include <linux/etherdevice.h>
#include "mt76.h"

static int mt76_get_of_eeprom_data(struct mt76_dev *dev, void *eep, int len)
{
	struct device_node *np = dev->dev->of_node;
	const void *data;
	int size;

	data = of_get_property(np, "mediatek,eeprom-data", &size);
	if (!data)
		return -ENOENT;

	if (size > len)
		return -EINVAL;

	memcpy(eep, data, size);

	return 0;
}

int mt76_get_of_data_from_mtd(struct mt76_dev *dev, void *eep, int offset, int len)
{
#ifdef CONFIG_MTD
	struct device_node *np = dev->dev->of_node;
	struct mtd_info *mtd;
	const __be32 *list;
	const char *part;
	phandle phandle;
	size_t retlen;
	int size;
	int ret;

	list = of_get_property(np, "mediatek,mtd-eeprom", &size);
	if (!list)
		return -ENOENT;

	phandle = be32_to_cpup(list++);
	if (!phandle)
		return -ENOENT;

	np = of_find_node_by_phandle(phandle);
	if (!np)
		return -EINVAL;

	part = of_get_property(np, "label", NULL);
	if (!part)
		part = np->name;

	mtd = get_mtd_device_nm(part);
	if (IS_ERR(mtd)) {
		ret =  PTR_ERR(mtd);
		goto out_put_node;
	}

	if (size <= sizeof(*list)) {
		ret = -EINVAL;
		goto out_put_node;
	}

	offset += be32_to_cpup(list);
	ret = mtd_read(mtd, offset, len, &retlen, eep);
	put_mtd_device(mtd);
	if (mtd_is_bitflip(ret))
		ret = 0;
	if (ret) {
		dev_err(dev->dev, "reading EEPROM from mtd %s failed: %i\n",
			part, ret);
		goto out_put_node;
	}

	if (retlen < len) {
		ret = -EINVAL;
		goto out_put_node;
	}

	if (of_property_read_bool(dev->dev->of_node, "big-endian")) {
		u8 *data = (u8 *)eep;
		int i;

		/* convert eeprom data in Little Endian */
		for (i = 0; i < round_down(len, 2); i += 2)
			put_unaligned_le16(get_unaligned_be16(&data[i]),
					   &data[i]);
	}

#ifdef CONFIG_NL80211_TESTMODE
	dev->test_mtd.name = devm_kstrdup(dev->dev, part, GFP_KERNEL);
	if (!dev->test_mtd.name) {
		ret = -ENOMEM;
		goto out_put_node;
	}
	dev->test_mtd.offset = offset;
#endif

out_put_node:
	of_node_put(np);
	return ret;
#else
	return -ENOENT;
#endif
}
EXPORT_SYMBOL_GPL(mt76_get_of_data_from_mtd);

int mt76_get_of_data_from_nvmem(struct mt76_dev *dev, void *eep,
				const char *cell_name, int len)
{
	struct device_node *np = dev->dev->of_node;
	struct nvmem_cell *cell;
	const void *data;
	size_t retlen;
	int ret = 0;

	cell = of_nvmem_cell_get(np, cell_name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	data = nvmem_cell_read(cell, &retlen);
	nvmem_cell_put(cell);

	if (IS_ERR(data))
		return PTR_ERR(data);

	if (retlen < len) {
		ret = -EINVAL;
		goto exit;
	}

	memcpy(eep, data, len);

exit:
	kfree(data);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_get_of_data_from_nvmem);

static int mt76_get_of_eeprom(struct mt76_dev *dev, void *eep, int len)
{
	struct device_node *np = dev->dev->of_node;
	int ret;

	if (!np)
		return -ENOENT;

	ret = mt76_get_of_eeprom_data(dev, eep, len);
	if (!ret)
		return 0;

	ret = mt76_get_of_data_from_mtd(dev, eep, 0, len);
	if (!ret)
		return 0;

	return mt76_get_of_data_from_nvmem(dev, eep, "eeprom", len);
}

int
mt76_eeprom_override(struct mt76_phy *phy)
{
	struct mt76_dev *dev = phy->dev;
	struct device_node *np = dev->dev->of_node;
	int err;

	err = of_get_mac_address(np, phy->macaddr);
	if (err == -EPROBE_DEFER)
		return err;

	if (!is_valid_ether_addr(phy->macaddr)) {
		eth_random_addr(phy->macaddr);
		dev_info(dev->dev,
			 "Invalid MAC address, using random address %pM\n",
			 phy->macaddr);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_eeprom_override);

static bool mt76_string_prop_find(struct property *prop, const char *str)
{
	const char *cp = NULL;

	if (!prop || !str || !str[0])
		return false;

	while ((cp = of_prop_next_string(prop, cp)) != NULL)
		if (!strcasecmp(cp, str))
			return true;

	return false;
}

struct device_node *
mt76_find_power_limits_node(struct mt76_dev *dev)
{
	struct device_node *np = dev->dev->of_node;
	const char *const region_names[] = {
		[NL80211_DFS_UNSET] = "ww",
		[NL80211_DFS_ETSI] = "etsi",
		[NL80211_DFS_FCC] = "fcc",
		[NL80211_DFS_JP] = "jp",
	};
	struct device_node *cur, *fallback = NULL;
	const char *region_name = NULL;

	if (dev->region < ARRAY_SIZE(region_names))
		region_name = region_names[dev->region];

	np = of_get_child_by_name(np, "power-limits");
	if (!np)
		return NULL;

	for_each_child_of_node(np, cur) {
		struct property *country = of_find_property(cur, "country", NULL);
		struct property *regd = of_find_property(cur, "regdomain", NULL);

		if (!country && !regd) {
			fallback = cur;
			continue;
		}

		if (mt76_string_prop_find(country, dev->alpha2) ||
		    mt76_string_prop_find(regd, region_name)) {
			of_node_put(np);
			return cur;
		}
	}

	of_node_put(np);
	return fallback;
}
EXPORT_SYMBOL_GPL(mt76_find_power_limits_node);

static const __be32 *
mt76_get_of_array(struct device_node *np, char *name, size_t *len, int min)
{
	struct property *prop = of_find_property(np, name, NULL);

	if (!prop || !prop->value || prop->length < min * 4)
		return NULL;

	*len = prop->length;

	return prop->value;
}

struct device_node *
mt76_find_channel_node(struct device_node *np, struct ieee80211_channel *chan)
{
	struct device_node *cur;
	const __be32 *val;
	size_t len;

	for_each_child_of_node(np, cur) {
		val = mt76_get_of_array(cur, "channels", &len, 2);
		if (!val)
			continue;

		while (len >= 2 * sizeof(*val)) {
			if (chan->hw_value >= be32_to_cpu(val[0]) &&
			    chan->hw_value <= be32_to_cpu(val[1]))
				return cur;

			val += 2;
			len -= 2 * sizeof(*val);
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(mt76_find_channel_node);


static s8
mt76_get_txs_delta(struct device_node *np, u8 nss)
{
	const __be32 *val;
	size_t len;

	val = mt76_get_of_array(np, "txs-delta", &len, nss);
	if (!val)
		return 0;

	return be32_to_cpu(val[nss - 1]);
}

static void
mt76_apply_array_limit(s8 *pwr, size_t pwr_len, const __be32 *data,
		       s8 target_power, s8 nss_delta, s8 *max_power)
{
	int i;

	if (!data)
		return;

	for (i = 0; i < pwr_len; i++) {
		pwr[i] = min_t(s8, target_power,
			       be32_to_cpu(data[i]) + nss_delta);
		*max_power = max(*max_power, pwr[i]);
	}
}

static void
mt76_apply_multi_array_limit(s8 *pwr, size_t pwr_len, s8 pwr_num,
			     const __be32 *data, size_t len, s8 target_power,
			     s8 nss_delta, s8 *max_power)
{
	int i, cur;

	if (!data)
		return;

	len /= 4;
	cur = be32_to_cpu(data[0]);
	for (i = 0; i < pwr_num; i++) {
		if (len < pwr_len + 1)
			break;

		mt76_apply_array_limit(pwr + pwr_len * i, pwr_len, data + 1,
				       target_power, nss_delta, max_power);
		if (--cur > 0)
			continue;

		data += pwr_len + 1;
		len -= pwr_len + 1;
		if (!len)
			break;

		cur = be32_to_cpu(data[0]);
	}
}

s8 mt76_get_rate_power_limits(struct mt76_phy *phy,
			      struct ieee80211_channel *chan,
			      struct mt76_power_limits *dest,
			      s8 target_power)
{
	struct mt76_dev *dev = phy->dev;
	struct device_node *np;
	const __be32 *val;
	char name[16];
	u32 mcs_rates = dev->drv->mcs_rates;
	u32 ru_rates = ARRAY_SIZE(dest->ru[0]);
	char band;
	size_t len;
	s8 max_power = 0;
	s8 txs_delta;

	if (!mcs_rates)
		mcs_rates = 10;

	memset(dest, target_power, sizeof(*dest));

	if (!IS_ENABLED(CONFIG_OF))
		return target_power;

	np = mt76_find_power_limits_node(dev);
	if (!np)
		return target_power;

	switch (chan->band) {
	case NL80211_BAND_2GHZ:
		band = '2';
		break;
	case NL80211_BAND_5GHZ:
		band = '5';
		break;
	case NL80211_BAND_6GHZ:
		band = '6';
		break;
	default:
		return target_power;
	}

	snprintf(name, sizeof(name), "txpower-%cg", band);
	np = of_get_child_by_name(np, name);
	if (!np)
		return target_power;

	np = mt76_find_channel_node(np, chan);
	if (!np)
		return target_power;

	txs_delta = mt76_get_txs_delta(np, hweight16(phy->chainmask));

	val = mt76_get_of_array(np, "rates-cck", &len, ARRAY_SIZE(dest->cck));
	mt76_apply_array_limit(dest->cck, ARRAY_SIZE(dest->cck), val,
			       target_power, txs_delta, &max_power);

	val = mt76_get_of_array(np, "rates-ofdm",
				&len, ARRAY_SIZE(dest->ofdm));
	mt76_apply_array_limit(dest->ofdm, ARRAY_SIZE(dest->ofdm), val,
			       target_power, txs_delta, &max_power);

	val = mt76_get_of_array(np, "rates-mcs", &len, mcs_rates + 1);
	mt76_apply_multi_array_limit(dest->mcs[0], ARRAY_SIZE(dest->mcs[0]),
				     ARRAY_SIZE(dest->mcs), val, len,
				     target_power, txs_delta, &max_power);

	val = mt76_get_of_array(np, "rates-ru", &len, ru_rates + 1);
	mt76_apply_multi_array_limit(dest->ru[0], ARRAY_SIZE(dest->ru[0]),
				     ARRAY_SIZE(dest->ru), val, len,
				     target_power, txs_delta, &max_power);

	return max_power;
}
EXPORT_SYMBOL_GPL(mt76_get_rate_power_limits);

int
mt76_eeprom_init(struct mt76_dev *dev, int len)
{
	dev->eeprom.size = len;
	dev->eeprom.data = devm_kzalloc(dev->dev, len, GFP_KERNEL);
	if (!dev->eeprom.data)
		return -ENOMEM;

	return !mt76_get_of_eeprom(dev, dev->eeprom.data, len);
}
EXPORT_SYMBOL_GPL(mt76_eeprom_init);
