/*
 * Copyright (c) 2017 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#define MESON_SOCINFO_MAJOR_VER_MESON6		0x16
#define MESON_SOCINFO_MAJOR_VER_MESON8		0x19
#define MESON_SOCINFO_MAJOR_VER_MESON8B		0x1b

#define MESON_MX_ASSIST_HW_REV			0x14c

#define MESON_MX_ANALOG_TOP_METAL_REVISION	0x0

#define MESON_MX_BOOTROM_MISC_VER		0x4

static const char *meson_mx_socinfo_revision(unsigned int major_ver,
					     unsigned int misc_ver,
					     unsigned int metal_rev)
{
	unsigned int minor_ver;

	switch (major_ver) {
	case MESON_SOCINFO_MAJOR_VER_MESON6:
		minor_ver = 0xa;
		break;

	case MESON_SOCINFO_MAJOR_VER_MESON8:
		if (metal_rev == 0x11111112)
			major_ver = 0x1d;

		if (metal_rev == 0x11111111 || metal_rev == 0x11111112)
			minor_ver = 0xa;
		else if (metal_rev == 0x11111113)
			minor_ver = 0xb;
		else if (metal_rev == 0x11111133)
			minor_ver = 0xc;
		else
			minor_ver = 0xd;

		break;

	case MESON_SOCINFO_MAJOR_VER_MESON8B:
		if (metal_rev == 0x11111111)
			minor_ver = 0xa;
		else
			minor_ver = 0xb;

		break;

	default:
		minor_ver = 0x0;
		break;
	}

	return kasprintf(GFP_KERNEL, "Rev%X (%x - 0:%X)", minor_ver, major_ver,
			 misc_ver);
}

static const char *meson_mx_socinfo_soc_id(unsigned int major_ver,
					   unsigned int metal_rev)
{
	const char *soc_id;

	switch (major_ver) {
	case MESON_SOCINFO_MAJOR_VER_MESON6:
		soc_id = "Meson6 (AML8726-MX)";
		break;

	case MESON_SOCINFO_MAJOR_VER_MESON8:
		if (metal_rev == 0x11111112)
			soc_id = "Meson8m2 (S812)";
		else
			soc_id = "Meson8 (S802)";

		break;

	case MESON_SOCINFO_MAJOR_VER_MESON8B:
		soc_id = "Meson8b (S805)";
		break;

	default:
		soc_id = "Unknown";
		break;
	}

	return kstrdup_const(soc_id, GFP_KERNEL);
}

static const struct of_device_id meson_mx_socinfo_analog_top_ids[] = {
	{ .compatible = "amlogic,meson8-analog-top", },
	{ .compatible = "amlogic,meson8b-analog-top", },
	{ /* sentinel */ }
};

static int __init meson_mx_socinfo_init(void)
{
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;
	struct device_node *np;
	struct regmap *assist_regmap, *bootrom_regmap, *analog_top_regmap;
	unsigned int major_ver, misc_ver, metal_rev = 0;
	int ret;

	assist_regmap =
		syscon_regmap_lookup_by_compatible("amlogic,meson-mx-assist");
	if (IS_ERR(assist_regmap))
		return PTR_ERR(assist_regmap);

	bootrom_regmap =
		syscon_regmap_lookup_by_compatible("amlogic,meson-mx-bootrom");
	if (IS_ERR(bootrom_regmap))
		return PTR_ERR(bootrom_regmap);

	np = of_find_matching_node(NULL, meson_mx_socinfo_analog_top_ids);
	if (np) {
		analog_top_regmap = syscon_node_to_regmap(np);
		if (IS_ERR(analog_top_regmap))
			return PTR_ERR(analog_top_regmap);

		ret = regmap_read(analog_top_regmap,
				  MESON_MX_ANALOG_TOP_METAL_REVISION,
				  &metal_rev);
		if (ret)
			return ret;
	}

	ret = regmap_read(assist_regmap, MESON_MX_ASSIST_HW_REV, &major_ver);
	if (ret < 0)
		return ret;

	ret = regmap_read(bootrom_regmap, MESON_MX_BOOTROM_MISC_VER,
			  &misc_ver);
	if (ret < 0)
		return ret;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENODEV;

	soc_dev_attr->family = "Amlogic Meson";

	np = of_find_node_by_path("/");
	of_property_read_string(np, "model", &soc_dev_attr->machine);
	of_node_put(np);

	soc_dev_attr->revision = meson_mx_socinfo_revision(major_ver, misc_ver,
							   metal_rev);
	soc_dev_attr->soc_id = meson_mx_socinfo_soc_id(major_ver, metal_rev);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree_const(soc_dev_attr->revision);
		kfree_const(soc_dev_attr->soc_id);
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	dev_info(soc_device_to_device(soc_dev), "Amlogic %s %s detected\n",
		 soc_dev_attr->soc_id, soc_dev_attr->revision);

	return 0;
}
device_initcall(meson_mx_socinfo_init);
