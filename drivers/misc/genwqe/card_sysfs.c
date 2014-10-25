/**
 * IBM Accelerator Family 'GenWQE'
 *
 * (C) Copyright IBM Corp. 2013
 *
 * Author: Frank Haverkamp <haver@linux.vnet.ibm.com>
 * Author: Joerg-Stephan Vogt <jsvogt@de.ibm.com>
 * Author: Michael Jung <mijung@gmx.net>
 * Author: Michael Ruettger <michael@ibmra.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * Sysfs interfaces for the GenWQE card. There are attributes to query
 * the version of the bitstream as well as some for the driver. For
 * debugging, please also see the debugfs interfaces of this driver.
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/ctype.h>
#include <linux/device.h>

#include "card_base.h"
#include "card_ddcb.h"

static const char * const genwqe_types[] = {
	[GENWQE_TYPE_ALTERA_230] = "GenWQE4-230",
	[GENWQE_TYPE_ALTERA_530] = "GenWQE4-530",
	[GENWQE_TYPE_ALTERA_A4]  = "GenWQE5-A4",
	[GENWQE_TYPE_ALTERA_A7]  = "GenWQE5-A7",
};

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct genwqe_dev *cd = dev_get_drvdata(dev);
	const char *cs[GENWQE_CARD_STATE_MAX] = { "unused", "used", "error" };

	return sprintf(buf, "%s\n", cs[cd->card_state]);
}
static DEVICE_ATTR_RO(status);

static ssize_t appid_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	char app_name[5];
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	genwqe_read_app_id(cd, app_name, sizeof(app_name));
	return sprintf(buf, "%s\n", app_name);
}
static DEVICE_ATTR_RO(appid);

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	u64 slu_id, app_id;
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	slu_id = __genwqe_readq(cd, IO_SLU_UNITCFG);
	app_id = __genwqe_readq(cd, IO_APP_UNITCFG);

	return sprintf(buf, "%016llx.%016llx\n", slu_id, app_id);
}
static DEVICE_ATTR_RO(version);

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	u8 card_type;
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	card_type = genwqe_card_type(cd);
	return sprintf(buf, "%s\n", (card_type >= ARRAY_SIZE(genwqe_types)) ?
		       "invalid" : genwqe_types[card_type]);
}
static DEVICE_ATTR_RO(type);

static ssize_t tempsens_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	u64 tempsens;
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	tempsens = __genwqe_readq(cd, IO_SLU_TEMPERATURE_SENSOR);
	return sprintf(buf, "%016llx\n", tempsens);
}
static DEVICE_ATTR_RO(tempsens);

static ssize_t freerunning_timer_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	u64 t;
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	t = __genwqe_readq(cd, IO_SLC_FREE_RUNNING_TIMER);
	return sprintf(buf, "%016llx\n", t);
}
static DEVICE_ATTR_RO(freerunning_timer);

static ssize_t queue_working_time_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	u64 t;
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	t = __genwqe_readq(cd, IO_SLC_QUEUE_WTIME);
	return sprintf(buf, "%016llx\n", t);
}
static DEVICE_ATTR_RO(queue_working_time);

static ssize_t base_clock_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	u64 base_clock;
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	base_clock = genwqe_base_clock_frequency(cd);
	return sprintf(buf, "%lld\n", base_clock);
}
static DEVICE_ATTR_RO(base_clock);

/**
 * curr_bitstream_show() - Show the current bitstream id
 *
 * There is a bug in some old versions of the CPLD which selects the
 * bitstream, which causes the IO_SLU_BITSTREAM register to report
 * unreliable data in very rare cases. This makes this sysfs
 * unreliable up to the point were a new CPLD version is being used.
 *
 * Unfortunately there is no automatic way yet to query the CPLD
 * version, such that you need to manually ensure via programming
 * tools that you have a recent version of the CPLD software.
 *
 * The proposed circumvention is to use a special recovery bitstream
 * on the backup partition (0) to identify problems while loading the
 * image.
 */
static ssize_t curr_bitstream_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int curr_bitstream;
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	curr_bitstream = __genwqe_readq(cd, IO_SLU_BITSTREAM) & 0x1;
	return sprintf(buf, "%d\n", curr_bitstream);
}
static DEVICE_ATTR_RO(curr_bitstream);

/**
 * next_bitstream_show() - Show the next activated bitstream
 *
 * IO_SLC_CFGREG_SOFTRESET: This register can only be accessed by the PF.
 */
static ssize_t next_bitstream_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int next_bitstream;
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	switch ((cd->softreset & 0xc) >> 2) {
	case 0x2:
		next_bitstream =  0;
		break;
	case 0x3:
		next_bitstream =  1;
		break;
	default:
		next_bitstream = -1;
		break;		/* error */
	}
	return sprintf(buf, "%d\n", next_bitstream);
}

static ssize_t next_bitstream_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int partition;
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	if (kstrtoint(buf, 0, &partition) < 0)
		return -EINVAL;

	switch (partition) {
	case 0x0:
		cd->softreset = 0x78;
		break;
	case 0x1:
		cd->softreset = 0x7c;
		break;
	default:
		return -EINVAL;
	}

	__genwqe_writeq(cd, IO_SLC_CFGREG_SOFTRESET, cd->softreset);
	return count;
}
static DEVICE_ATTR_RW(next_bitstream);

static ssize_t reload_bitstream_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int reload;
	struct genwqe_dev *cd = dev_get_drvdata(dev);

	if (kstrtoint(buf, 0, &reload) < 0)
		return -EINVAL;

	if (reload == 0x1) {
		if (cd->card_state == GENWQE_CARD_UNUSED ||
		    cd->card_state == GENWQE_CARD_USED)
			cd->card_state = GENWQE_CARD_RELOAD_BITSTREAM;
		else
			return -EIO;
	} else {
		return -EINVAL;
	}

	return count;
}
static DEVICE_ATTR_WO(reload_bitstream);

/*
 * Create device_attribute structures / params: name, mode, show, store
 * additional flag if valid in VF
 */
static struct attribute *genwqe_attributes[] = {
	&dev_attr_tempsens.attr,
	&dev_attr_next_bitstream.attr,
	&dev_attr_curr_bitstream.attr,
	&dev_attr_base_clock.attr,
	&dev_attr_type.attr,
	&dev_attr_version.attr,
	&dev_attr_appid.attr,
	&dev_attr_status.attr,
	&dev_attr_freerunning_timer.attr,
	&dev_attr_queue_working_time.attr,
	&dev_attr_reload_bitstream.attr,
	NULL,
};

static struct attribute *genwqe_normal_attributes[] = {
	&dev_attr_type.attr,
	&dev_attr_version.attr,
	&dev_attr_appid.attr,
	&dev_attr_status.attr,
	&dev_attr_freerunning_timer.attr,
	&dev_attr_queue_working_time.attr,
	NULL,
};

/**
 * genwqe_is_visible() - Determine if sysfs attribute should be visible or not
 *
 * VFs have restricted mmio capabilities, so not all sysfs entries
 * are allowed in VFs.
 */
static umode_t genwqe_is_visible(struct kobject *kobj,
				 struct attribute *attr, int n)
{
	unsigned int j;
	struct device *dev = container_of(kobj, struct device, kobj);
	struct genwqe_dev *cd = dev_get_drvdata(dev);
	umode_t mode = attr->mode;

	if (genwqe_is_privileged(cd))
		return mode;

	for (j = 0; genwqe_normal_attributes[j] != NULL;  j++)
		if (genwqe_normal_attributes[j] == attr)
			return mode;

	return 0;
}

static struct attribute_group genwqe_attribute_group = {
	.is_visible = genwqe_is_visible,
	.attrs      = genwqe_attributes,
};

const struct attribute_group *genwqe_attribute_groups[] = {
	&genwqe_attribute_group,
	NULL,
};
