// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Aspeed Technology Inc.
 *
 * IMX3102: 2-to-1 multiplexier
 *
 * +------------------   +
 * | SoC                 |
 * |                     |
 * | I3C controller #0 - | --+
 * |                     |    \                  dev   dev
 * |                     |     +---------+       |     |
 * |                     |     | IMX3102 | ---+--+--+--+--- i3c bus
 * |                     |     +---------+    |     |
 * |                     |    /               dev   dev
 * | I3C controller #1 - | --+
 * |                     |
 * +---------------------+
 */

#include <linux/i3c/device.h>
#include <linux/i3c/master.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include "internals.h"

#define IMX3102_DEVICE_TYPE_HI		0x0
#define IMX3102_DEVICE_TYPE_LO		0x1

#define IMX3102_PORT_CONF		0x40
#define   IMX3102_PORT_CONF_M1_EN	BIT(7)
#define   IMX3102_PORT_CONF_S_EN	BIT(6)
#define IMX3102_PORT_SEL		0x41
#define   IMX3102_PORT_SEL_M1		BIT(7)
#define   IMX3102_PORT_SEL_S_EN		BIT(6)

struct imx3102 {
	struct regmap *regmap;

	struct bin_attribute ownership;
	struct bin_attribute reinit;
	struct kernfs_node *kn;

	struct i3c_device *i3cdev;
};

static ssize_t i3c_mux_imx3102_query(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *attr, char *buf,
				     loff_t pos, size_t count)
{
	struct imx3102 *imx3102;
	struct device *dev;
	int ret;
	u8 data[2];

	imx3102 = dev_get_drvdata(container_of(kobj, struct device, kobj));
	if (!imx3102)
		return -1;

	dev = &imx3102->i3cdev->dev;

	ret = regmap_raw_read(imx3102->regmap, IMX3102_DEVICE_TYPE_HI, data, 2);
	if (ret)
		sprintf(buf, "N\n");
	else
		sprintf(buf, "Y\n");

	return 2;
}

/* write whatever value to imx3102-mux to release the ownership */
static ssize_t i3c_mux_imx3102_release_chan(struct file *filp,
					    struct kobject *kobj,
					    struct bin_attribute *attr,
					    char *buf, loff_t pos, size_t count)
{
	struct imx3102 *imx3102;
	struct device *dev;
	struct regmap *regmap;
	int ret;
	u8 select;

	imx3102 = dev_get_drvdata(container_of(kobj, struct device, kobj));
	if (!imx3102) {
		count = -1;
		goto out;
	}

	dev = &imx3102->i3cdev->dev;
	regmap = imx3102->regmap;
	ret = regmap_raw_read(regmap, IMX3102_PORT_SEL, &select, 1);
	if (ret)
		goto out;

	/* invert the bit to change the ownership */
	select ^= IMX3102_PORT_SEL_M1;
	regmap_raw_write(regmap, IMX3102_PORT_SEL, &select, 1);

out:
	return count;
}

static ssize_t i3c_mux_imx3102_bus_reinit(struct file *filp,
					  struct kobject *kobj,
					  struct bin_attribute *attr, char *buf,
					  loff_t pos, size_t count)
{
	struct imx3102 *imx3102;
	int ret;

	imx3102 = dev_get_drvdata(container_of(kobj, struct device, kobj));
	if (!imx3102) {
		count = -1;
		return count;
	}

	ret = i3c_device_setaasa_ccc(imx3102->i3cdev);
	ret = i3c_device_sethid_ccc(imx3102->i3cdev);

	return count;
}

static int i3c_mux_imx3102_probe(struct i3c_device *i3cdev)
{
	struct device *dev = &i3cdev->dev;
	struct imx3102 *imx3102;
	struct regmap *regmap;
	struct regmap_config imx3102_i3c_regmap_config = {
		.reg_bits = 8,
		.pad_bits = 8,
		.val_bits = 8,
	};
	int ret;
	u8 data[2];

	if (dev->type == &i3c_masterdev_type)
		return -ENOTSUPP;

	imx3102 = devm_kzalloc(dev, sizeof(*imx3102), GFP_KERNEL);
	if (!imx3102)
		return -ENOMEM;

	imx3102->i3cdev = i3cdev;

	/* register regmap */
	regmap = devm_regmap_init_i3c(i3cdev, &imx3102_i3c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to register i3c regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}
	imx3102->regmap = regmap;

	sysfs_bin_attr_init(&imx3102->ownership);
	imx3102->ownership.attr.name = "imx3102.ownership";
	imx3102->ownership.attr.mode = 0600;
	imx3102->ownership.read = i3c_mux_imx3102_query;
	imx3102->ownership.write = i3c_mux_imx3102_release_chan;
	imx3102->ownership.size = 2;
	ret = sysfs_create_bin_file(&dev->kobj, &imx3102->ownership);

	sysfs_bin_attr_init(&imx3102->reinit);
	imx3102->reinit.attr.name = "imx3102.reinit";
	imx3102->reinit.attr.mode = 0200;
	imx3102->reinit.write = i3c_mux_imx3102_bus_reinit;
	imx3102->reinit.size = 2;
	ret = sysfs_create_bin_file(&dev->kobj, &imx3102->reinit);

	imx3102->kn = kernfs_find_and_get(dev->kobj.sd, imx3102->ownership.attr.name);
	dev_set_drvdata(dev, imx3102);

	ret = regmap_raw_read(regmap, IMX3102_DEVICE_TYPE_HI, data, 2);
	if (ret) {
		dev_info(dev, "No ownership\n");
		return 0;
	}
	dev_dbg(dev, "device ID %02x %02x\n", data[0], data[1]);

	/* enable the slave port */
	regmap_raw_read(regmap, IMX3102_PORT_CONF, &data[0], 2);
	data[0] |= IMX3102_PORT_CONF_S_EN | IMX3102_PORT_CONF_M1_EN;
	data[1] |= IMX3102_PORT_SEL_S_EN;
	regmap_raw_write(regmap, IMX3102_PORT_CONF, data, 2);

	/* send SETAASA to bring the devices behind the mux to I3C mode */
	i3c_device_setaasa_ccc(i3cdev);

	return 0;
}

static void i3c_mux_imx3102_remove(struct i3c_device *i3cdev)
{
	struct device *dev = &i3cdev->dev;
	struct imx3102 *imx3102;

	imx3102 = dev_get_drvdata(dev);

	kernfs_put(imx3102->kn);
	sysfs_remove_bin_file(&dev->kobj, &imx3102->ownership);
	devm_kfree(dev, imx3102);
}

static const struct i3c_device_id i3c_mux_imx3102_ids[] = {
	I3C_DEVICE(0x266, 0x3102, (void *)0),
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i3c, i3c_mux_imx3102_ids);

static struct i3c_driver imx3102_driver = {
	.driver = {
		.name = "i3c-mux-imx3102",
	},
	.probe = i3c_mux_imx3102_probe,
	.remove = i3c_mux_imx3102_remove,
	.id_table = i3c_mux_imx3102_ids,
};
module_i3c_driver(imx3102_driver);

MODULE_AUTHOR("Dylan Hung <dylan_hung@aspeedtech.com>");
MODULE_DESCRIPTION("I3C IMX3102 multiplexer driver");
MODULE_LICENSE("GPL");
