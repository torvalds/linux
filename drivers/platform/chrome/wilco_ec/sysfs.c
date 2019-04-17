// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 *
 * Sysfs properties to view and modify EC-controlled features on Wilco devices.
 * The entries will appear under /sys/bus/platform/devices/GOOG000C:00/
 *
 * See Documentation/ABI/testing/sysfs-platform-wilco-ec for more information.
 */

#include <linux/platform_data/wilco-ec.h>
#include <linux/sysfs.h>

#define CMD_KB_CMOS			0x7C
#define SUB_CMD_KB_CMOS_AUTO_ON		0x03

struct boot_on_ac_request {
	u8 cmd;			/* Always CMD_KB_CMOS */
	u8 reserved1;
	u8 sub_cmd;		/* Always SUB_CMD_KB_CMOS_AUTO_ON */
	u8 reserved3to5[3];
	u8 val;			/* Either 0 or 1 */
	u8 reserved7;
} __packed;

static ssize_t boot_on_ac_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct wilco_ec_device *ec = dev_get_drvdata(dev);
	struct boot_on_ac_request rq;
	struct wilco_ec_message msg;
	int ret;
	u8 val;

	ret = kstrtou8(buf, 10, &val);
	if (ret < 0)
		return ret;
	if (val > 1)
		return -EINVAL;

	memset(&rq, 0, sizeof(rq));
	rq.cmd = CMD_KB_CMOS;
	rq.sub_cmd = SUB_CMD_KB_CMOS_AUTO_ON;
	rq.val = val;

	memset(&msg, 0, sizeof(msg));
	msg.type = WILCO_EC_MSG_LEGACY;
	msg.request_data = &rq;
	msg.request_size = sizeof(rq);
	ret = wilco_ec_mailbox(ec, &msg);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR_WO(boot_on_ac);

static struct attribute *wilco_dev_attrs[] = {
	&dev_attr_boot_on_ac.attr,
	NULL,
};

static struct attribute_group wilco_dev_attr_group = {
	.attrs = wilco_dev_attrs,
};

int wilco_ec_add_sysfs(struct wilco_ec_device *ec)
{
	return sysfs_create_group(&ec->dev->kobj, &wilco_dev_attr_group);
}

void wilco_ec_remove_sysfs(struct wilco_ec_device *ec)
{
	sysfs_remove_group(&ec->dev->kobj, &wilco_dev_attr_group);
}
