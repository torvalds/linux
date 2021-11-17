/*
 * drivers/sh/superhyway/superhyway-sysfs.c
 *
 * SuperHyway Bus sysfs interface
 *
 * Copyright (C) 2004, 2005  Paul Mundt <lethal@linux-sh.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/superhyway.h>

#define superhyway_ro_attr(name, fmt, field)				\
static ssize_t name##_show(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct superhyway_device *s = to_superhyway_device(dev);	\
	return sprintf(buf, fmt, s->field);				\
}									\
static DEVICE_ATTR_RO(name);

/* VCR flags */
superhyway_ro_attr(perr_flags, "0x%02x\n", vcr.perr_flags);
superhyway_ro_attr(merr_flags, "0x%02x\n", vcr.merr_flags);
superhyway_ro_attr(mod_vers, "0x%04x\n", vcr.mod_vers);
superhyway_ro_attr(mod_id, "0x%04x\n", vcr.mod_id);
superhyway_ro_attr(bot_mb, "0x%02x\n", vcr.bot_mb);
superhyway_ro_attr(top_mb, "0x%02x\n", vcr.top_mb);

/* Misc */
superhyway_ro_attr(resource, "0x%08lx\n", resource[0].start);

static struct attribute *superhyway_dev_attrs[] = {
	&dev_attr_perr_flags.attr,
	&dev_attr_merr_flags.attr,
	&dev_attr_mod_vers.attr,
	&dev_attr_mod_id.attr,
	&dev_attr_bot_mb.attr,
	&dev_attr_top_mb.attr,
	&dev_attr_resource.attr,
	NULL,
};

static const struct attribute_group superhyway_dev_group = {
	.attrs = superhyway_dev_attrs,
};

const struct attribute_group *superhyway_dev_groups[] = {
	&superhyway_dev_group,
	NULL,
};
