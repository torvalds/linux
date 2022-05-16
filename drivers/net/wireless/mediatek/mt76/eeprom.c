// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/etherdevice.h>
#include "mt76.h"

static int
mt76_get_of_eeprom(struct mt76_dev *dev, int len)
{
#if defined(CONFIG_OF) && defined(CONFIG_MTD)
	struct device_node *np = dev->dev->of_node;
	struct mtd_info *mtd;
	const __be32 *list;
	const char *part;
	phandle phandle;
	int offset = 0;
	int size;
	size_t retlen;
	int ret;

	if (!np)
		return -ENOENT;

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

	offset = be32_to_cpup(list);
	ret = mtd_read(mtd, offset, len, &retlen, dev->eeprom.data);
	put_mtd_device(mtd);
	if (ret)
		goto out_put_node;

	if (retlen < len) {
		ret = -EINVAL;
		goto out_put_node;
	}

	if (of_property_read_bool(dev->dev->of_node, "big-endian")) {
		u8 *data = (u8 *)dev->eeprom.data;
		int i;

		/* convert eeprom data in Little Endian */
		for (i = 0; i < round_down(len, 2); i += 2)
			put_unaligned_le16(get_unaligned_be16(&data[i]),
					   &data[i]);
	}

#ifdef CONFIG_NL80211_TESTMODE
	dev->test.mtd_name = devm_kstrdup(dev->dev, part, GFP_KERNEL);
	dev->test.mtd_offset = offset;
#endif

out_put_node:
	of_node_put(np);
	return ret;
#else
	return -ENOENT;
#endif
}

void
mt76_eeprom_override(struct mt76_dev *dev)
{
#ifdef CONFIG_OF
	struct device_node *np = dev->dev->of_node;
	const u8 *mac = NULL;

	if (np)
		mac = of_get_mac_address(np);
	if (!IS_ERR_OR_NULL(mac))
		ether_addr_copy(dev->macaddr, mac);
#endif

	if (!is_valid_ether_addr(dev->macaddr)) {
		eth_random_addr(dev->macaddr);
		dev_info(dev->dev,
			 "Invalid MAC address, using random address %pM\n",
			 dev->macaddr);
	}
}
EXPORT_SYMBOL_GPL(mt76_eeprom_override);

int
mt76_eeprom_init(struct mt76_dev *dev, int len)
{
	dev->eeprom.size = len;
	dev->eeprom.data = devm_kzalloc(dev->dev, len, GFP_KERNEL);
	if (!dev->eeprom.data)
		return -ENOMEM;

	return !mt76_get_of_eeprom(dev, len);
}
EXPORT_SYMBOL_GPL(mt76_eeprom_init);
