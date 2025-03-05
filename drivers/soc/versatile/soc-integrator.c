// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Linaro Ltd.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/of.h>

#define INTEGRATOR_HDR_ID_OFFSET	0x00

static u32 integrator_coreid;

static const struct of_device_id integrator_cm_match[] = {
	{ .compatible = "arm,core-module-integrator", },
	{ }
};

static const char *integrator_arch_str(u32 id)
{
	switch ((id >> 16) & 0xff) {
	case 0x00:
		return "ASB little-endian";
	case 0x01:
		return "AHB little-endian";
	case 0x03:
		return "AHB-Lite system bus, bi-endian";
	case 0x04:
		return "AHB";
	case 0x08:
		return "AHB system bus, ASB processor bus";
	default:
		return "Unknown";
	}
}

static const char *integrator_fpga_str(u32 id)
{
	switch ((id >> 12) & 0xf) {
	case 0x01:
		return "XC4062";
	case 0x02:
		return "XC4085";
	case 0x03:
		return "XVC600";
	case 0x04:
		return "EPM7256AE (Altera PLD)";
	default:
		return "Unknown";
	}
}

static ssize_t
manufacturer_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%02x\n", integrator_coreid >> 24);
}

static DEVICE_ATTR_RO(manufacturer);

static ssize_t
arch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", integrator_arch_str(integrator_coreid));
}

static DEVICE_ATTR_RO(arch);

static ssize_t
fpga_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", integrator_fpga_str(integrator_coreid));
}

static DEVICE_ATTR_RO(fpga);

static ssize_t
build_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%02x\n", (integrator_coreid >> 4) & 0xFF);
}

static DEVICE_ATTR_RO(build);

static struct attribute *integrator_attrs[] = {
	&dev_attr_manufacturer.attr,
	&dev_attr_arch.attr,
	&dev_attr_fpga.attr,
	&dev_attr_build.attr,
	NULL
};

ATTRIBUTE_GROUPS(integrator);

static int __init integrator_soc_init(void)
{
	struct regmap *syscon_regmap;
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;
	struct device_node *np;
	struct device *dev;
	u32 val;
	int ret;

	np = of_find_matching_node(NULL, integrator_cm_match);
	if (!np)
		return -ENODEV;

	syscon_regmap = syscon_node_to_regmap(np);
	of_node_put(np);
	if (IS_ERR(syscon_regmap))
		return PTR_ERR(syscon_regmap);

	ret = regmap_read(syscon_regmap, INTEGRATOR_HDR_ID_OFFSET,
			  &val);
	if (ret)
		return -ENODEV;
	integrator_coreid = val;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->soc_id = "Integrator";
	soc_dev_attr->machine = "Integrator";
	soc_dev_attr->family = "Versatile";
	soc_dev_attr->custom_attr_group = integrator_groups[0];
	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr);
		return -ENODEV;
	}
	dev = soc_device_to_device(soc_dev);

	dev_info(dev, "Detected ARM core module:\n");
	dev_info(dev, "    Manufacturer: %02x\n", (val >> 24));
	dev_info(dev, "    Architecture: %s\n", integrator_arch_str(val));
	dev_info(dev, "    FPGA: %s\n", integrator_fpga_str(val));
	dev_info(dev, "    Build: %02x\n", (val >> 4) & 0xFF);
	dev_info(dev, "    Rev: %c\n", ('A' + (val & 0x03)));

	return 0;
}
device_initcall(integrator_soc_init);
