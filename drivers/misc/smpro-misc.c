// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ampere Computing SoC's SMpro Misc Driver
 *
 * Copyright (c) 2022, Ampere Computing LLC
 */
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* Boot Stage/Progress Registers */
#define BOOTSTAGE	0xB0
#define BOOTSTAGE_LO	0xB1
#define CUR_BOOTSTAGE	0xB2
#define BOOTSTAGE_HI	0xB3

/* SOC State Registers */
#define SOC_POWER_LIMIT		0xE5

struct smpro_misc {
	struct regmap *regmap;
};

static ssize_t boot_progress_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct smpro_misc *misc = dev_get_drvdata(dev);
	u16 boot_progress[3] = { 0 };
	u32 bootstage;
	u8 boot_stage;
	u8 cur_stage;
	u32 reg_lo;
	u32 reg;
	int ret;

	/* Read current boot stage */
	ret = regmap_read(misc->regmap, CUR_BOOTSTAGE, &reg);
	if (ret)
		return ret;

	cur_stage = reg & 0xff;

	ret = regmap_read(misc->regmap, BOOTSTAGE, &bootstage);
	if (ret)
		return ret;

	boot_stage = (bootstage >> 8) & 0xff;

	if (boot_stage > cur_stage)
		return -EINVAL;

	ret = regmap_read(misc->regmap,	BOOTSTAGE_LO, &reg_lo);
	if (!ret)
		ret = regmap_read(misc->regmap, BOOTSTAGE_HI, &reg);
	if (ret)
		return ret;

	/* Firmware to report new boot stage next time */
	if (boot_stage < cur_stage) {
		ret = regmap_write(misc->regmap, BOOTSTAGE, ((bootstage & 0xff00) | 0x1));
		if (ret)
			return ret;
	}

	boot_progress[0] = bootstage;
	boot_progress[1] = swab16(reg);
	boot_progress[2] = swab16(reg_lo);

	return sysfs_emit(buf, "%*phN\n", (int)sizeof(boot_progress), boot_progress);
}

static DEVICE_ATTR_RO(boot_progress);

static ssize_t soc_power_limit_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct smpro_misc *misc = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(misc->regmap, SOC_POWER_LIMIT, &value);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t soc_power_limit_store(struct device *dev, struct device_attribute *da,
				     const char *buf, size_t count)
{
	struct smpro_misc *misc = dev_get_drvdata(dev);
	unsigned long val;
	s32 ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	ret = regmap_write(misc->regmap, SOC_POWER_LIMIT, (unsigned int)val);
	if (ret)
		return -EPROTO;

	return count;
}

static DEVICE_ATTR_RW(soc_power_limit);

static struct attribute *smpro_misc_attrs[] = {
	&dev_attr_boot_progress.attr,
	&dev_attr_soc_power_limit.attr,
	NULL
};

ATTRIBUTE_GROUPS(smpro_misc);

static int smpro_misc_probe(struct platform_device *pdev)
{
	struct smpro_misc *misc;

	misc = devm_kzalloc(&pdev->dev, sizeof(struct smpro_misc), GFP_KERNEL);
	if (!misc)
		return -ENOMEM;

	platform_set_drvdata(pdev, misc);

	misc->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!misc->regmap)
		return -ENODEV;

	return 0;
}

static struct platform_driver smpro_misc_driver = {
	.probe		= smpro_misc_probe,
	.driver = {
		.name	= "smpro-misc",
		.dev_groups = smpro_misc_groups,
	},
};

module_platform_driver(smpro_misc_driver);

MODULE_AUTHOR("Tung Nguyen <tungnguyen@os.amperecomputing.com>");
MODULE_AUTHOR("Quan Nguyen <quan@os.amperecomputing.com>");
MODULE_DESCRIPTION("Ampere Altra SMpro Misc driver");
MODULE_LICENSE("GPL");
