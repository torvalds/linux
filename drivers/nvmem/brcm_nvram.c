// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/bcm47xx_nvram.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define NVRAM_MAGIC			"FLSH"

struct brcm_nvram {
	struct device *dev;
	void __iomem *base;
	struct nvmem_cell_info *cells;
	int ncells;
};

struct brcm_nvram_header {
	char magic[4];
	__le32 len;
	__le32 crc_ver_init;	/* 0:7 crc, 8:15 ver, 16:31 sdram_init */
	__le32 config_refresh;	/* 0:15 sdram_config, 16:31 sdram_refresh */
	__le32 config_ncdl;	/* ncdl values for memc */
};

static int brcm_nvram_read(void *context, unsigned int offset, void *val,
			   size_t bytes)
{
	struct brcm_nvram *priv = context;
	u8 *dst = val;

	while (bytes--)
		*dst++ = readb(priv->base + offset++);

	return 0;
}

static int brcm_nvram_read_post_process_macaddr(void *context, const char *id, int index,
						unsigned int offset, void *buf, size_t bytes)
{
	u8 mac[ETH_ALEN];

	if (bytes != 3 * ETH_ALEN - 1)
		return -EINVAL;

	if (!mac_pton(buf, mac))
		return -EINVAL;

	if (index)
		eth_addr_add(mac, index);

	ether_addr_copy(buf, mac);

	return 0;
}

static int brcm_nvram_add_cells(struct brcm_nvram *priv, uint8_t *data,
				size_t len)
{
	struct device *dev = priv->dev;
	char *var, *value, *eq;
	int idx;

	priv->ncells = 0;
	for (var = data + sizeof(struct brcm_nvram_header);
	     var < (char *)data + len && *var;
	     var += strlen(var) + 1) {
		priv->ncells++;
	}

	priv->cells = devm_kcalloc(dev, priv->ncells, sizeof(*priv->cells), GFP_KERNEL);
	if (!priv->cells)
		return -ENOMEM;

	for (var = data + sizeof(struct brcm_nvram_header), idx = 0;
	     var < (char *)data + len && *var;
	     var = value + strlen(value) + 1, idx++) {
		eq = strchr(var, '=');
		if (!eq)
			break;
		*eq = '\0';
		value = eq + 1;

		priv->cells[idx].name = devm_kstrdup(dev, var, GFP_KERNEL);
		if (!priv->cells[idx].name)
			return -ENOMEM;
		priv->cells[idx].offset = value - (char *)data;
		priv->cells[idx].bytes = strlen(value);
		priv->cells[idx].np = of_get_child_by_name(dev->of_node, priv->cells[idx].name);
		if (!strcmp(var, "et0macaddr") ||
		    !strcmp(var, "et1macaddr") ||
		    !strcmp(var, "et2macaddr")) {
			priv->cells[idx].raw_len = strlen(value);
			priv->cells[idx].bytes = ETH_ALEN;
			priv->cells[idx].read_post_process = brcm_nvram_read_post_process_macaddr;
		}
	}

	return 0;
}

static int brcm_nvram_parse(struct brcm_nvram *priv)
{
	struct device *dev = priv->dev;
	struct brcm_nvram_header header;
	uint8_t *data;
	size_t len;
	int err;

	memcpy_fromio(&header, priv->base, sizeof(header));

	if (memcmp(header.magic, NVRAM_MAGIC, 4)) {
		dev_err(dev, "Invalid NVRAM magic\n");
		return -EINVAL;
	}

	len = le32_to_cpu(header.len);

	data = kzalloc(len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy_fromio(data, priv->base, len);
	data[len - 1] = '\0';

	err = brcm_nvram_add_cells(priv, data, len);
	if (err) {
		dev_err(dev, "Failed to add cells: %d\n", err);
		return err;
	}

	kfree(data);

	return 0;
}

static int brcm_nvram_probe(struct platform_device *pdev)
{
	struct nvmem_config config = {
		.name = "brcm-nvram",
		.reg_read = brcm_nvram_read,
	};
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct brcm_nvram *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	priv->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	err = brcm_nvram_parse(priv);
	if (err)
		return err;

	bcm47xx_nvram_init_from_iomem(priv->base, resource_size(res));

	config.dev = dev;
	config.cells = priv->cells;
	config.ncells = priv->ncells;
	config.priv = priv;
	config.size = resource_size(res);

	return PTR_ERR_OR_ZERO(devm_nvmem_register(dev, &config));
}

static const struct of_device_id brcm_nvram_of_match_table[] = {
	{ .compatible = "brcm,nvram", },
	{},
};

static struct platform_driver brcm_nvram_driver = {
	.probe = brcm_nvram_probe,
	.driver = {
		.name = "brcm_nvram",
		.of_match_table = brcm_nvram_of_match_table,
	},
};

static int __init brcm_nvram_init(void)
{
	return platform_driver_register(&brcm_nvram_driver);
}

subsys_initcall_sync(brcm_nvram_init);

MODULE_AUTHOR("Rafał Miłecki");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, brcm_nvram_of_match_table);
