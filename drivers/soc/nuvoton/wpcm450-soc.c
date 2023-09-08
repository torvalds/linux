// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton WPCM450 SoC Identification
 *
 * Copyright (C) 2022 Jonathan Neuschäfer
 */

#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#define GCR_PDID	0
#define PDID_CHIP(x)	((x) & 0x00ffffff)
#define CHIP_WPCM450	0x926450
#define PDID_REV(x)	((x) >> 24)

struct revision {
	u8 number;
	const char *name;
};

static const struct revision revisions[] __initconst = {
	{ 0x00, "Z1" },
	{ 0x03, "Z2" },
	{ 0x04, "Z21" },
	{ 0x08, "A1" },
	{ 0x09, "A2" },
	{ 0x0a, "A3" },
	{}
};

static const char * __init get_revision(unsigned int rev)
{
	int i;

	for (i = 0; revisions[i].name; i++)
		if (revisions[i].number == rev)
			return revisions[i].name;
	return NULL;
}

static struct soc_device_attribute *wpcm450_attr;
static struct soc_device *wpcm450_soc;

static int __init wpcm450_soc_init(void)
{
	struct soc_device_attribute *attr;
	struct soc_device *soc;
	const char *revision;
	struct regmap *gcr;
	u32 pdid;
	int ret;

	if (!of_machine_is_compatible("nuvoton,wpcm450"))
		return 0;

	gcr = syscon_regmap_lookup_by_compatible("nuvoton,wpcm450-gcr");
	if (IS_ERR(gcr))
		return PTR_ERR(gcr);
	ret = regmap_read(gcr, GCR_PDID, &pdid);
	if (ret)
		return ret;

	if (PDID_CHIP(pdid) != CHIP_WPCM450) {
		pr_warn("Unknown chip ID in GCR.PDID: 0x%06x\n", PDID_CHIP(pdid));
		return -ENODEV;
	}

	revision = get_revision(PDID_REV(pdid));
	if (!revision) {
		pr_warn("Unknown chip revision in GCR.PDID: 0x%02x\n", PDID_REV(pdid));
		return -ENODEV;
	}

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	attr->family = "Nuvoton NPCM";
	attr->soc_id = "WPCM450";
	attr->revision = revision;
	soc = soc_device_register(attr);
	if (IS_ERR(soc)) {
		kfree(attr);
		pr_warn("Could not register SoC device\n");
		return PTR_ERR(soc);
	}

	wpcm450_soc = soc;
	wpcm450_attr = attr;
	return 0;
}
module_init(wpcm450_soc_init);

static void __exit wpcm450_soc_exit(void)
{
	if (wpcm450_soc) {
		soc_device_unregister(wpcm450_soc);
		wpcm450_soc = NULL;
		kfree(wpcm450_attr);
	}
}
module_exit(wpcm450_soc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonathan Neuschäfer");
MODULE_DESCRIPTION("Nuvoton WPCM450 SoC Identification driver");
