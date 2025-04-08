// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Nicolas Saenz Julienne <nsaenzjulienne@suse.de>
 */

#include <linux/crc32.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct rmem {
	struct device *dev;
	struct nvmem_device *nvmem;
	struct reserved_mem *mem;
};

struct rmem_match_data {
	int (*checksum)(struct rmem *priv);
};

struct __packed rmem_eyeq5_header {
	u32 magic;
	u32 version;
	u32 size;
};

#define RMEM_EYEQ5_MAGIC	((u32)0xDABBAD00)

static int rmem_read(void *context, unsigned int offset,
		     void *val, size_t bytes)
{
	struct rmem *priv = context;
	void *addr;

	if ((phys_addr_t)offset + bytes > priv->mem->size)
		return -EIO;

	/*
	 * Only map the reserved memory at this point to avoid potential rogue
	 * kernel threads inadvertently modifying it. Based on the current
	 * uses-cases for this driver, the performance hit isn't a concern.
	 * Nor is likely to be, given the nature of the subsystem. Most nvmem
	 * devices operate over slow buses to begin with.
	 *
	 * An alternative would be setting the memory as RO, set_memory_ro(),
	 * but as of Dec 2020 this isn't possible on arm64.
	 */
	addr = memremap(priv->mem->base, priv->mem->size, MEMREMAP_WB);
	if (!addr) {
		dev_err(priv->dev, "Failed to remap memory region\n");
		return -ENOMEM;
	}

	memcpy(val, addr + offset, bytes);

	memunmap(addr);

	return 0;
}

static int rmem_eyeq5_checksum(struct rmem *priv)
{
	void *buf __free(kfree) = NULL;
	struct rmem_eyeq5_header header;
	u32 computed_crc, *target_crc;
	size_t data_size;
	int ret;

	ret = rmem_read(priv, 0, &header, sizeof(header));
	if (ret)
		return ret;

	if (header.magic != RMEM_EYEQ5_MAGIC)
		return -EINVAL;

	/*
	 * Avoid massive kmalloc() if header read is invalid;
	 * the check would be done by the next rmem_read() anyway.
	 */
	if (header.size > priv->mem->size)
		return -EINVAL;

	/*
	 *           0 +-------------------+
	 *             | Header (12 bytes) | \
	 *             +-------------------+ |
	 *             |                   | | data to be CRCed
	 *             |        ...        | |
	 *             |                   | /
	 *   data_size +-------------------+
	 *             |   CRC (4 bytes)   |
	 * header.size +-------------------+
	 */

	buf = kmalloc(header.size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = rmem_read(priv, 0, buf, header.size);
	if (ret)
		return ret;

	data_size = header.size - sizeof(*target_crc);
	target_crc = buf + data_size;
	computed_crc = crc32(U32_MAX, buf, data_size) ^ U32_MAX;

	if (computed_crc == *target_crc)
		return 0;

	dev_err(priv->dev,
		"checksum failed: computed %#x, expected %#x, header (%#x, %#x, %#x)\n",
		computed_crc, *target_crc, header.magic, header.version, header.size);
	return -EINVAL;
}

static int rmem_probe(struct platform_device *pdev)
{
	struct nvmem_config config = { };
	struct device *dev = &pdev->dev;
	const struct rmem_match_data *match_data = device_get_match_data(dev);
	struct reserved_mem *mem;
	struct rmem *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	mem = of_reserved_mem_lookup(dev->of_node);
	if (!mem) {
		dev_err(dev, "Failed to lookup reserved memory\n");
		return -EINVAL;
	}
	priv->mem = mem;

	config.dev = dev;
	config.priv = priv;
	config.name = "rmem";
	config.id = NVMEM_DEVID_AUTO;
	config.size = mem->size;
	config.reg_read = rmem_read;

	if (match_data && match_data->checksum) {
		int ret = match_data->checksum(priv);

		if (ret)
			return ret;
	}

	return PTR_ERR_OR_ZERO(devm_nvmem_register(dev, &config));
}

static const struct rmem_match_data rmem_eyeq5_match_data = {
	.checksum = rmem_eyeq5_checksum,
};

static const struct of_device_id rmem_match[] = {
	{ .compatible = "mobileye,eyeq5-bootloader-config", .data = &rmem_eyeq5_match_data },
	{ .compatible = "nvmem-rmem", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rmem_match);

static struct platform_driver rmem_driver = {
	.probe = rmem_probe,
	.driver = {
		.name = "rmem",
		.of_match_table = rmem_match,
	},
};
module_platform_driver(rmem_driver);

MODULE_AUTHOR("Nicolas Saenz Julienne <nsaenzjulienne@suse.de>");
MODULE_DESCRIPTION("Reserved Memory Based nvmem Driver");
MODULE_LICENSE("GPL");
