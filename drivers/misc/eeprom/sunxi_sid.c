/*
 * Copyright (c) 2013 Oliver Schinagl <oliver@schinagl.nl>
 * http://www.linux-sunxi.org
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
 * This driver exposes the Allwinner security ID, efuses exported in byte-
 * sized chunks.
 */

#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#define DRV_NAME "sunxi-sid"

struct sunxi_sid_data {
	void __iomem *reg_base;
	unsigned int keysize;
};

/* We read the entire key, due to a 32 bit read alignment requirement. Since we
 * want to return the requested byte, this results in somewhat slower code and
 * uses 4 times more reads as needed but keeps code simpler. Since the SID is
 * only very rarely probed, this is not really an issue.
 */
static u8 sunxi_sid_read_byte(const struct sunxi_sid_data *sid_data,
			      const unsigned int offset)
{
	u32 sid_key;

	if (offset >= sid_data->keysize)
		return 0;

	sid_key = ioread32be(sid_data->reg_base + round_down(offset, 4));
	sid_key >>= (offset % 4) * 8;

	return sid_key; /* Only return the last byte */
}

static ssize_t sid_read(struct file *fd, struct kobject *kobj,
			struct bin_attribute *attr, char *buf,
			loff_t pos, size_t size)
{
	struct platform_device *pdev;
	struct sunxi_sid_data *sid_data;
	int i;

	pdev = to_platform_device(kobj_to_dev(kobj));
	sid_data = platform_get_drvdata(pdev);

	if (pos < 0 || pos >= sid_data->keysize)
		return 0;
	if (size > sid_data->keysize - pos)
		size = sid_data->keysize - pos;

	for (i = 0; i < size; i++)
		buf[i] = sunxi_sid_read_byte(sid_data, pos + i);

	return i;
}

static struct bin_attribute sid_bin_attr = {
	.attr = { .name = "eeprom", .mode = S_IRUGO, },
	.read = sid_read,
};

static int sunxi_sid_remove(struct platform_device *pdev)
{
	device_remove_bin_file(&pdev->dev, &sid_bin_attr);
	dev_dbg(&pdev->dev, "driver unloaded\n");

	return 0;
}

static const struct of_device_id sunxi_sid_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-sid", .data = (void *)16},
	{ .compatible = "allwinner,sun7i-a20-sid", .data = (void *)512},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, sunxi_sid_of_match);

static int sunxi_sid_probe(struct platform_device *pdev)
{
	struct sunxi_sid_data *sid_data;
	struct resource *res;
	const struct of_device_id *of_dev_id;
	u8 *entropy;
	unsigned int i;

	sid_data = devm_kzalloc(&pdev->dev, sizeof(struct sunxi_sid_data),
				GFP_KERNEL);
	if (!sid_data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sid_data->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sid_data->reg_base))
		return PTR_ERR(sid_data->reg_base);

	of_dev_id = of_match_device(sunxi_sid_of_match, &pdev->dev);
	if (!of_dev_id)
		return -ENODEV;
	sid_data->keysize = (int)of_dev_id->data;

	platform_set_drvdata(pdev, sid_data);

	sid_bin_attr.size = sid_data->keysize;
	if (device_create_bin_file(&pdev->dev, &sid_bin_attr))
		return -ENODEV;

	entropy = kzalloc(sizeof(u8) * sid_data->keysize, GFP_KERNEL);
	for (i = 0; i < sid_data->keysize; i++)
		entropy[i] = sunxi_sid_read_byte(sid_data, i);
	add_device_randomness(entropy, sid_data->keysize);
	kfree(entropy);

	dev_dbg(&pdev->dev, "loaded\n");

	return 0;
}

static struct platform_driver sunxi_sid_driver = {
	.probe = sunxi_sid_probe,
	.remove = sunxi_sid_remove,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sunxi_sid_of_match,
	},
};
module_platform_driver(sunxi_sid_driver);

MODULE_AUTHOR("Oliver Schinagl <oliver@schinagl.nl>");
MODULE_DESCRIPTION("Allwinner sunxi security id driver");
MODULE_LICENSE("GPL");
