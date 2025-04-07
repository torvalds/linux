// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RapidIO sysfs attributes and support
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 */

#include <linux/kernel.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/stat.h>
#include <linux/capability.h>

#include "rio.h"

/* Sysfs support */
#define rio_config_attr(field, format_string)					\
static ssize_t								\
field##_show(struct device *dev, struct device_attribute *attr, char *buf)			\
{									\
	struct rio_dev *rdev = to_rio_dev(dev);				\
									\
	return sprintf(buf, format_string, rdev->field);		\
}									\
static DEVICE_ATTR_RO(field);

rio_config_attr(did, "0x%04x\n");
rio_config_attr(vid, "0x%04x\n");
rio_config_attr(device_rev, "0x%08x\n");
rio_config_attr(asm_did, "0x%04x\n");
rio_config_attr(asm_vid, "0x%04x\n");
rio_config_attr(asm_rev, "0x%04x\n");
rio_config_attr(destid, "0x%04x\n");
rio_config_attr(hopcount, "0x%02x\n");

static ssize_t routes_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	char *str = buf;
	int i;

	for (i = 0; i < RIO_MAX_ROUTE_ENTRIES(rdev->net->hport->sys_size);
			i++) {
		if (rdev->rswitch->route_table[i] == RIO_INVALID_ROUTE)
			continue;
		str +=
		    sprintf(str, "%04x %02x\n", i,
			    rdev->rswitch->route_table[i]);
	}

	return (str - buf);
}
static DEVICE_ATTR_RO(routes);

static ssize_t lprev_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);

	return sprintf(buf, "%s\n",
			(rdev->prev) ? rio_name(rdev->prev) : "root");
}
static DEVICE_ATTR_RO(lprev);

static ssize_t lnext_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	char *str = buf;
	int i;

	if (rdev->pef & RIO_PEF_SWITCH) {
		for (i = 0; i < RIO_GET_TOTAL_PORTS(rdev->swpinfo); i++) {
			if (rdev->rswitch->nextdev[i])
				str += sprintf(str, "%s\n",
					rio_name(rdev->rswitch->nextdev[i]));
			else
				str += sprintf(str, "null\n");
		}
	}

	return str - buf;
}
static DEVICE_ATTR_RO(lnext);

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);

	return sprintf(buf, "rapidio:v%04Xd%04Xav%04Xad%04X\n",
		       rdev->vid, rdev->did, rdev->asm_vid, rdev->asm_did);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *rio_dev_attrs[] = {
	&dev_attr_did.attr,
	&dev_attr_vid.attr,
	&dev_attr_device_rev.attr,
	&dev_attr_asm_did.attr,
	&dev_attr_asm_vid.attr,
	&dev_attr_asm_rev.attr,
	&dev_attr_lprev.attr,
	&dev_attr_destid.attr,
	&dev_attr_modalias.attr,

	/* Switch-only attributes */
	&dev_attr_routes.attr,
	&dev_attr_lnext.attr,
	&dev_attr_hopcount.attr,
	NULL,
};

static ssize_t
rio_read_config(struct file *filp, struct kobject *kobj,
		const struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct rio_dev *dev = to_rio_dev(kobj_to_dev(kobj));
	unsigned int size = 0x100;
	loff_t init_off = off;
	u8 *data = (u8 *) buf;

	/* Several chips lock up trying to read undefined config space */
	if (capable(CAP_SYS_ADMIN))
		size = RIO_MAINT_SPACE_SZ;

	if (off >= size)
		return 0;
	if (off + count > size) {
		size -= off;
		count = size;
	} else {
		size = count;
	}

	if ((off & 1) && size) {
		u8 val;
		rio_read_config_8(dev, off, &val);
		data[off - init_off] = val;
		off++;
		size--;
	}

	if ((off & 3) && size > 2) {
		u16 val;
		rio_read_config_16(dev, off, &val);
		data[off - init_off] = (val >> 8) & 0xff;
		data[off - init_off + 1] = val & 0xff;
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val;
		rio_read_config_32(dev, off, &val);
		data[off - init_off] = (val >> 24) & 0xff;
		data[off - init_off + 1] = (val >> 16) & 0xff;
		data[off - init_off + 2] = (val >> 8) & 0xff;
		data[off - init_off + 3] = val & 0xff;
		off += 4;
		size -= 4;
	}

	if (size >= 2) {
		u16 val;
		rio_read_config_16(dev, off, &val);
		data[off - init_off] = (val >> 8) & 0xff;
		data[off - init_off + 1] = val & 0xff;
		off += 2;
		size -= 2;
	}

	if (size > 0) {
		u8 val;
		rio_read_config_8(dev, off, &val);
		data[off - init_off] = val;
		off++;
		--size;
	}

	return count;
}

static ssize_t
rio_write_config(struct file *filp, struct kobject *kobj,
		 const struct bin_attribute *bin_attr,
		 char *buf, loff_t off, size_t count)
{
	struct rio_dev *dev = to_rio_dev(kobj_to_dev(kobj));
	unsigned int size = count;
	loff_t init_off = off;
	u8 *data = (u8 *) buf;

	if (off >= RIO_MAINT_SPACE_SZ)
		return 0;
	if (off + count > RIO_MAINT_SPACE_SZ) {
		size = RIO_MAINT_SPACE_SZ - off;
		count = size;
	}

	if ((off & 1) && size) {
		rio_write_config_8(dev, off, data[off - init_off]);
		off++;
		size--;
	}

	if ((off & 3) && (size > 2)) {
		u16 val = data[off - init_off + 1];
		val |= (u16) data[off - init_off] << 8;
		rio_write_config_16(dev, off, val);
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val = data[off - init_off + 3];
		val |= (u32) data[off - init_off + 2] << 8;
		val |= (u32) data[off - init_off + 1] << 16;
		val |= (u32) data[off - init_off] << 24;
		rio_write_config_32(dev, off, val);
		off += 4;
		size -= 4;
	}

	if (size >= 2) {
		u16 val = data[off - init_off + 1];
		val |= (u16) data[off - init_off] << 8;
		rio_write_config_16(dev, off, val);
		off += 2;
		size -= 2;
	}

	if (size) {
		rio_write_config_8(dev, off, data[off - init_off]);
		off++;
		--size;
	}

	return count;
}

static const struct bin_attribute rio_config_attr = {
	.attr = {
		 .name = "config",
		 .mode = S_IRUGO | S_IWUSR,
		 },
	.size = RIO_MAINT_SPACE_SZ,
	.read_new = rio_read_config,
	.write_new = rio_write_config,
};

static const struct bin_attribute *const rio_dev_bin_attrs[] = {
	&rio_config_attr,
	NULL,
};

static umode_t rio_dev_is_attr_visible(struct kobject *kobj,
				       struct attribute *attr, int n)
{
	struct rio_dev *rdev = to_rio_dev(kobj_to_dev(kobj));
	umode_t mode = attr->mode;

	if (!(rdev->pef & RIO_PEF_SWITCH) &&
	    (attr == &dev_attr_routes.attr ||
	     attr == &dev_attr_lnext.attr ||
	     attr == &dev_attr_hopcount.attr)) {
		/*
		 * Hide switch-specific attributes for a non-switch device.
		 */
		mode = 0;
	}

	return mode;
}

static const struct attribute_group rio_dev_group = {
	.attrs		= rio_dev_attrs,
	.is_visible	= rio_dev_is_attr_visible,
	.bin_attrs_new	= rio_dev_bin_attrs,
};

const struct attribute_group *rio_dev_groups[] = {
	&rio_dev_group,
	NULL,
};

static ssize_t scan_store(const struct bus_type *bus, const char *buf, size_t count)
{
	long val;
	int rc;

	if (kstrtol(buf, 0, &val) < 0)
		return -EINVAL;

	if (val == RIO_MPORT_ANY) {
		rc = rio_init_mports();
		goto exit;
	}

	if (val < 0 || val >= RIO_MAX_MPORTS)
		return -EINVAL;

	rc = rio_mport_scan((int)val);
exit:
	if (!rc)
		rc = count;

	return rc;
}
static BUS_ATTR_WO(scan);

static struct attribute *rio_bus_attrs[] = {
	&bus_attr_scan.attr,
	NULL,
};

static const struct attribute_group rio_bus_group = {
	.attrs = rio_bus_attrs,
};

const struct attribute_group *rio_bus_groups[] = {
	&rio_bus_group,
	NULL,
};

static ssize_t
port_destid_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	struct rio_mport *mport = to_rio_mport(dev);

	if (mport)
		return sprintf(buf, "0x%04x\n", mport->host_deviceid);
	else
		return -ENODEV;
}
static DEVICE_ATTR_RO(port_destid);

static ssize_t sys_size_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct rio_mport *mport = to_rio_mport(dev);

	if (mport)
		return sprintf(buf, "%u\n", mport->sys_size);
	else
		return -ENODEV;
}
static DEVICE_ATTR_RO(sys_size);

static struct attribute *rio_mport_attrs[] = {
	&dev_attr_port_destid.attr,
	&dev_attr_sys_size.attr,
	NULL,
};

static const struct attribute_group rio_mport_group = {
	.attrs = rio_mport_attrs,
};

const struct attribute_group *rio_mport_groups[] = {
	&rio_mport_group,
	NULL,
};
