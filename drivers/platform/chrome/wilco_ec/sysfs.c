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

#define CMD_USB_CHARGE 0x39

enum usb_charge_op {
	USB_CHARGE_GET = 0,
	USB_CHARGE_SET = 1,
};

struct usb_charge_request {
	u8 cmd;		/* Always CMD_USB_CHARGE */
	u8 reserved;
	u8 op;		/* One of enum usb_charge_op */
	u8 val;		/* When setting, either 0 or 1 */
} __packed;

struct usb_charge_response {
	u8 reserved;
	u8 status;	/* Set by EC to 0 on success, other value on failure */
	u8 val;		/* When getting, set by EC to either 0 or 1 */
} __packed;

#define CMD_EC_INFO			0x38
enum get_ec_info_op {
	CMD_GET_EC_LABEL	= 0,
	CMD_GET_EC_REV		= 1,
	CMD_GET_EC_MODEL	= 2,
	CMD_GET_EC_BUILD_DATE	= 3,
};

struct get_ec_info_req {
	u8 cmd;			/* Always CMD_EC_INFO */
	u8 reserved;
	u8 op;			/* One of enum get_ec_info_op */
} __packed;

struct get_ec_info_resp {
	u8 reserved[2];
	char value[9]; /* __nonstring: might not be null terminated */
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

static ssize_t get_info(struct device *dev, char *buf, enum get_ec_info_op op)
{
	struct wilco_ec_device *ec = dev_get_drvdata(dev);
	struct get_ec_info_req req = { .cmd = CMD_EC_INFO, .op = op };
	struct get_ec_info_resp resp;
	int ret;

	struct wilco_ec_message msg = {
		.type = WILCO_EC_MSG_LEGACY,
		.request_data = &req,
		.request_size = sizeof(req),
		.response_data = &resp,
		.response_size = sizeof(resp),
	};

	ret = wilco_ec_mailbox(ec, &msg);
	if (ret < 0)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%.*s\n", (int)sizeof(resp.value),
			 (char *)&resp.value);
}

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	return get_info(dev, buf, CMD_GET_EC_LABEL);
}

static DEVICE_ATTR_RO(version);

static ssize_t build_revision_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return get_info(dev, buf, CMD_GET_EC_REV);
}

static DEVICE_ATTR_RO(build_revision);

static ssize_t build_date_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return get_info(dev, buf, CMD_GET_EC_BUILD_DATE);
}

static DEVICE_ATTR_RO(build_date);

static ssize_t model_number_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return get_info(dev, buf, CMD_GET_EC_MODEL);
}

static DEVICE_ATTR_RO(model_number);

static int send_usb_charge(struct wilco_ec_device *ec,
				struct usb_charge_request *rq,
				struct usb_charge_response *rs)
{
	struct wilco_ec_message msg;
	int ret;

	memset(&msg, 0, sizeof(msg));
	msg.type = WILCO_EC_MSG_LEGACY;
	msg.request_data = rq;
	msg.request_size = sizeof(*rq);
	msg.response_data = rs;
	msg.response_size = sizeof(*rs);
	ret = wilco_ec_mailbox(ec, &msg);
	if (ret < 0)
		return ret;
	if (rs->status)
		return -EIO;

	return 0;
}

static ssize_t usb_charge_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct wilco_ec_device *ec = dev_get_drvdata(dev);
	struct usb_charge_request rq;
	struct usb_charge_response rs;
	int ret;

	memset(&rq, 0, sizeof(rq));
	rq.cmd = CMD_USB_CHARGE;
	rq.op = USB_CHARGE_GET;

	ret = send_usb_charge(ec, &rq, &rs);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", rs.val);
}

static ssize_t usb_charge_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct wilco_ec_device *ec = dev_get_drvdata(dev);
	struct usb_charge_request rq;
	struct usb_charge_response rs;
	int ret;
	u8 val;

	ret = kstrtou8(buf, 10, &val);
	if (ret < 0)
		return ret;
	if (val > 1)
		return -EINVAL;

	memset(&rq, 0, sizeof(rq));
	rq.cmd = CMD_USB_CHARGE;
	rq.op = USB_CHARGE_SET;
	rq.val = val;

	ret = send_usb_charge(ec, &rq, &rs);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(usb_charge);

static struct attribute *wilco_dev_attrs[] = {
	&dev_attr_boot_on_ac.attr,
	&dev_attr_build_date.attr,
	&dev_attr_build_revision.attr,
	&dev_attr_model_number.attr,
	&dev_attr_usb_charge.attr,
	&dev_attr_version.attr,
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
