/*
 * PowerNV LPC bus handling.
 *
 * Copyright 2013 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/bug.h>
#include <linux/gfp.h>
#include <linux/slab.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/opal.h>
#include <asm/scom.h>

/*
 * We could probably fit that inside the scom_map_t
 * which is a void* after all but it's really too ugly
 * so let's kmalloc it for now
 */
struct opal_scom_map {
	uint32_t chip;
	uint64_t addr;
};

static scom_map_t opal_scom_map(struct device_node *dev, u64 reg, u64 count)
{
	struct opal_scom_map *m;
	const __be32 *gcid;

	if (!of_get_property(dev, "scom-controller", NULL)) {
		pr_err("%s: device %s is not a SCOM controller\n",
			__func__, dev->full_name);
		return SCOM_MAP_INVALID;
	}
	gcid = of_get_property(dev, "ibm,chip-id", NULL);
	if (!gcid) {
		pr_err("%s: device %s has no ibm,chip-id\n",
			__func__, dev->full_name);
		return SCOM_MAP_INVALID;
	}
	m = kmalloc(sizeof(struct opal_scom_map), GFP_KERNEL);
	if (!m)
		return NULL;
	m->chip = be32_to_cpup(gcid);
	m->addr = reg;

	return (scom_map_t)m;
}

static void opal_scom_unmap(scom_map_t map)
{
	kfree(map);
}

static int opal_xscom_err_xlate(int64_t rc)
{
	switch(rc) {
	case 0:
		return 0;
	/* Add more translations if necessary */
	default:
		return -EIO;
	}
}

static u64 opal_scom_unmangle(u64 addr)
{
	/*
	 * XSCOM indirect addresses have the top bit set. Additionally
	 * the rest of the top 3 nibbles is always 0.
	 *
	 * Because the debugfs interface uses signed offsets and shifts
	 * the address left by 3, we basically cannot use the top 4 bits
	 * of the 64-bit address, and thus cannot use the indirect bit.
	 *
	 * To deal with that, we support the indirect bit being in bit
	 * 4 (IBM notation) instead of bit 0 in this API, we do the
	 * conversion here. To leave room for further xscom address
	 * expansion, we only clear out the top byte
	 *
	 * For in-kernel use, we also support the real indirect bit, so
	 * we test for any of the top 5 bits
	 *
	 */
	if (addr & (0x1full << 59))
		addr = (addr & ~(0xffull << 56)) | (1ull << 63);
	return addr;
}

static int opal_scom_read(scom_map_t map, u64 reg, u64 *value)
{
	struct opal_scom_map *m = map;
	int64_t rc;
	__be64 v;

	reg = opal_scom_unmangle(m->addr + reg);
	rc = opal_xscom_read(m->chip, reg, (__be64 *)__pa(&v));
	*value = be64_to_cpu(v);
	return opal_xscom_err_xlate(rc);
}

static int opal_scom_write(scom_map_t map, u64 reg, u64 value)
{
	struct opal_scom_map *m = map;
	int64_t rc;

	reg = opal_scom_unmangle(m->addr + reg);
	rc = opal_xscom_write(m->chip, reg, value);
	return opal_xscom_err_xlate(rc);
}

static const struct scom_controller opal_scom_controller = {
	.map	= opal_scom_map,
	.unmap	= opal_scom_unmap,
	.read	= opal_scom_read,
	.write	= opal_scom_write
};

static int opal_xscom_init(void)
{
	if (firmware_has_feature(FW_FEATURE_OPALv3))
		scom_init(&opal_scom_controller);
	return 0;
}
arch_initcall(opal_xscom_init);
