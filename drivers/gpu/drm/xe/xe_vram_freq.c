// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */
#include <linux/sysfs.h>
#include <drm/drm_managed.h>

#include "xe_gt_types.h"
#include "xe_pcode.h"
#include "xe_pcode_api.h"
#include "xe_tile.h"
#include "xe_tile_sysfs.h"
#include "xe_vram_freq.h"

/**
 * DOC: Xe VRAM freq
 *
 * Provides sysfs entries for vram frequency in tile
 *
 * device/tile#/memory/freq0/max_freq - This is maximum frequency. This value is read-only as it
 *					is the fixed fuse point P0. It is not the system
 *					configuration.
 * device/tile#/memory/freq0/min_freq - This is minimum frequency. This value is read-only as it
 *					is the fixed fuse point PN. It is not the system
 *					configuration.
 */

static struct xe_tile *dev_to_tile(struct device *dev)
{
	return kobj_to_tile(dev->kobj.parent);
}

static ssize_t max_freq_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct xe_tile *tile = dev_to_tile(dev);
	u32 val = 0, mbox;
	int err;

	mbox = REG_FIELD_PREP(PCODE_MB_COMMAND, PCODE_FREQUENCY_CONFIG)
		| REG_FIELD_PREP(PCODE_MB_PARAM1, PCODE_MBOX_FC_SC_READ_FUSED_P0)
		| REG_FIELD_PREP(PCODE_MB_PARAM2, PCODE_MBOX_DOMAIN_HBM);

	err = xe_pcode_read(tile, mbox, &val, NULL);
	if (err)
		return err;

	/* data_out - Fused P0 for domain ID in units of 50 MHz */
	val *= 50;

	return sysfs_emit(buf, "%u\n", val);
}
static DEVICE_ATTR_RO(max_freq);

static ssize_t min_freq_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct xe_tile *tile = dev_to_tile(dev);
	u32 val = 0, mbox;
	int err;

	mbox = REG_FIELD_PREP(PCODE_MB_COMMAND, PCODE_FREQUENCY_CONFIG)
		| REG_FIELD_PREP(PCODE_MB_PARAM1, PCODE_MBOX_FC_SC_READ_FUSED_PN)
		| REG_FIELD_PREP(PCODE_MB_PARAM2, PCODE_MBOX_DOMAIN_HBM);

	err = xe_pcode_read(tile, mbox, &val, NULL);
	if (err)
		return err;

	/* data_out - Fused Pn for domain ID in units of 50 MHz */
	val *= 50;

	return sysfs_emit(buf, "%u\n", val);
}
static DEVICE_ATTR_RO(min_freq);

static struct attribute *freq_attrs[] = {
	&dev_attr_max_freq.attr,
	&dev_attr_min_freq.attr,
	NULL
};

static const struct attribute_group freq_group_attrs = {
	.name = "freq0",
	.attrs = freq_attrs,
};

static void vram_freq_sysfs_fini(void *arg)
{
	struct kobject *kobj = arg;

	sysfs_remove_group(kobj, &freq_group_attrs);
	kobject_put(kobj);
}

/**
 * xe_vram_freq_sysfs_init - Initialize vram frequency sysfs component
 * @tile: Xe Tile object
 *
 * It needs to be initialized after the main tile component is ready
 *
 * Returns: 0 on success, negative error code on error.
 */
int xe_vram_freq_sysfs_init(struct xe_tile *tile)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct kobject *kobj;
	int err;

	if (xe->info.platform != XE_PVC)
		return 0;

	kobj = kobject_create_and_add("memory", tile->sysfs);
	if (!kobj)
		return -ENOMEM;

	err = sysfs_create_group(kobj, &freq_group_attrs);
	if (err) {
		kobject_put(kobj);
		return err;
	}

	return devm_add_action_or_reset(xe->drm.dev, vram_freq_sysfs_fini, kobj);
}
