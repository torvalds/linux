/*
 * Copyright (C) 2014 Linaro Ltd.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
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

static ssize_t integrator_get_manf(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%02x\n", integrator_coreid >> 24);
}

static struct device_attribute integrator_manf_attr =
	__ATTR(manufacturer,  S_IRUGO, integrator_get_manf,  NULL);

static ssize_t integrator_get_arch(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%s\n", integrator_arch_str(integrator_coreid));
}

static struct device_attribute integrator_arch_attr =
	__ATTR(arch,  S_IRUGO, integrator_get_arch,  NULL);

static ssize_t integrator_get_fpga(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%s\n", integrator_fpga_str(integrator_coreid));
}

static struct device_attribute integrator_fpga_attr =
	__ATTR(fpga,  S_IRUGO, integrator_get_fpga,  NULL);

static ssize_t integrator_get_build(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return sprintf(buf, "%02x\n", (integrator_coreid >> 4) & 0xFF);
}

static struct device_attribute integrator_build_attr =
	__ATTR(build,  S_IRUGO, integrator_get_build,  NULL);

static int __init integrator_soc_init(void)
{
	static struct regmap *syscon_regmap;
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
	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr);
		return -ENODEV;
	}
	dev = soc_device_to_device(soc_dev);

	device_create_file(dev, &integrator_manf_attr);
	device_create_file(dev, &integrator_arch_attr);
	device_create_file(dev, &integrator_fpga_attr);
	device_create_file(dev, &integrator_build_attr);

	dev_info(dev, "Detected ARM core module:\n");
	dev_info(dev, "    Manufacturer: %02x\n", (val >> 24));
	dev_info(dev, "    Architecture: %s\n", integrator_arch_str(val));
	dev_info(dev, "    FPGA: %s\n", integrator_fpga_str(val));
	dev_info(dev, "    Build: %02x\n", (val >> 4) & 0xFF);
	dev_info(dev, "    Rev: %c\n", ('A' + (val & 0x03)));

	return 0;
}
device_initcall(integrator_soc_init);
