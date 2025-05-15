// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - 2023 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/crc32.h>
#include <linux/etherdevice.h>
#include <linux/export.h>
#include <linux/if_ether.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "u-boot-env.h"

struct u_boot_env_image_single {
	__le32 crc32;
	uint8_t data[];
} __packed;

struct u_boot_env_image_redundant {
	__le32 crc32;
	u8 mark;
	uint8_t data[];
} __packed;

struct u_boot_env_image_broadcom {
	__le32 magic;
	__le32 len;
	__le32 crc32;
	DECLARE_FLEX_ARRAY(uint8_t, data);
} __packed;

static int u_boot_env_read_post_process_ethaddr(void *context, const char *id, int index,
						unsigned int offset, void *buf, size_t bytes)
{
	u8 mac[ETH_ALEN];

	if (bytes != MAC_ADDR_STR_LEN)
		return -EINVAL;

	if (!mac_pton(buf, mac))
		return -EINVAL;

	if (index)
		eth_addr_add(mac, index);

	ether_addr_copy(buf, mac);

	return 0;
}

static int u_boot_env_parse_cells(struct device *dev, struct nvmem_device *nvmem, uint8_t *buf,
				  size_t data_offset, size_t data_len)
{
	char *data = buf + data_offset;
	char *var, *value, *eq;

	for (var = data;
	     var < data + data_len && *var;
	     var = value + strlen(value) + 1) {
		struct nvmem_cell_info info = {};

		eq = strchr(var, '=');
		if (!eq)
			break;
		*eq = '\0';
		value = eq + 1;

		info.name = devm_kstrdup(dev, var, GFP_KERNEL);
		if (!info.name)
			return -ENOMEM;
		info.offset = data_offset + value - data;
		info.bytes = strlen(value);
		info.np = of_get_child_by_name(dev->of_node, info.name);
		if (!strcmp(var, "ethaddr")) {
			info.raw_len = strlen(value);
			info.bytes = ETH_ALEN;
			info.read_post_process = u_boot_env_read_post_process_ethaddr;
		}

		nvmem_add_one_cell(nvmem, &info);
	}

	return 0;
}

int u_boot_env_parse(struct device *dev, struct nvmem_device *nvmem,
		     enum u_boot_env_format format)
{
	size_t crc32_data_offset;
	size_t crc32_data_len;
	size_t crc32_offset;
	__le32 *crc32_addr;
	size_t data_offset;
	size_t data_len;
	size_t dev_size;
	uint32_t crc32;
	uint32_t calc;
	uint8_t *buf;
	int bytes;
	int err;

	dev_size = nvmem_dev_size(nvmem);

	buf = kzalloc(dev_size, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto err_out;
	}

	bytes = nvmem_device_read(nvmem, 0, dev_size, buf);
	if (bytes < 0) {
		err = bytes;
		goto err_kfree;
	} else if (bytes != dev_size) {
		err = -EIO;
		goto err_kfree;
	}

	switch (format) {
	case U_BOOT_FORMAT_SINGLE:
		crc32_offset = offsetof(struct u_boot_env_image_single, crc32);
		crc32_data_offset = offsetof(struct u_boot_env_image_single, data);
		data_offset = offsetof(struct u_boot_env_image_single, data);
		break;
	case U_BOOT_FORMAT_REDUNDANT:
		crc32_offset = offsetof(struct u_boot_env_image_redundant, crc32);
		crc32_data_offset = offsetof(struct u_boot_env_image_redundant, data);
		data_offset = offsetof(struct u_boot_env_image_redundant, data);
		break;
	case U_BOOT_FORMAT_BROADCOM:
		crc32_offset = offsetof(struct u_boot_env_image_broadcom, crc32);
		crc32_data_offset = offsetof(struct u_boot_env_image_broadcom, data);
		data_offset = offsetof(struct u_boot_env_image_broadcom, data);
		break;
	}

	if (dev_size < data_offset) {
		dev_err(dev, "Device too small for u-boot-env\n");
		err = -EIO;
		goto err_kfree;
	}

	crc32_addr = (__le32 *)(buf + crc32_offset);
	crc32 = le32_to_cpu(*crc32_addr);
	crc32_data_len = dev_size - crc32_data_offset;
	data_len = dev_size - data_offset;

	calc = crc32(~0, buf + crc32_data_offset, crc32_data_len) ^ ~0L;
	if (calc != crc32) {
		dev_err(dev, "Invalid calculated CRC32: 0x%08x (expected: 0x%08x)\n", calc, crc32);
		err = -EINVAL;
		goto err_kfree;
	}

	buf[dev_size - 1] = '\0';
	err = u_boot_env_parse_cells(dev, nvmem, buf, data_offset, data_len);

err_kfree:
	kfree(buf);
err_out:
	return err;
}
EXPORT_SYMBOL_GPL(u_boot_env_parse);

static int u_boot_env_add_cells(struct nvmem_layout *layout)
{
	struct device *dev = &layout->dev;
	enum u_boot_env_format format;

	format = (uintptr_t)device_get_match_data(dev);

	return u_boot_env_parse(dev, layout->nvmem, format);
}

static int u_boot_env_probe(struct nvmem_layout *layout)
{
	layout->add_cells = u_boot_env_add_cells;

	return nvmem_layout_register(layout);
}

static void u_boot_env_remove(struct nvmem_layout *layout)
{
	nvmem_layout_unregister(layout);
}

static const struct of_device_id u_boot_env_of_match_table[] = {
	{ .compatible = "u-boot,env", .data = (void *)U_BOOT_FORMAT_SINGLE, },
	{ .compatible = "u-boot,env-redundant-bool", .data = (void *)U_BOOT_FORMAT_REDUNDANT, },
	{ .compatible = "u-boot,env-redundant-count", .data = (void *)U_BOOT_FORMAT_REDUNDANT, },
	{ .compatible = "brcm,env", .data = (void *)U_BOOT_FORMAT_BROADCOM, },
	{},
};

static struct nvmem_layout_driver u_boot_env_layout = {
	.driver = {
		.name = "u-boot-env-layout",
		.of_match_table = u_boot_env_of_match_table,
	},
	.probe = u_boot_env_probe,
	.remove = u_boot_env_remove,
};
module_nvmem_layout_driver(u_boot_env_layout);

MODULE_AUTHOR("Rafał Miłecki");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, u_boot_env_of_match_table);
MODULE_DESCRIPTION("NVMEM layout driver for U-Boot environment variables");
