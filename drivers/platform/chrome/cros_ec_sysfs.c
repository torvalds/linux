/*
 * cros_ec_sysfs - expose the Chrome OS EC through sysfs
 *
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "cros_ec_sysfs: " fmt

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "cros_ec_dev.h"

/* Accessor functions */

static ssize_t show_ec_reboot(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	int count = 0;

	count += scnprintf(buf + count, PAGE_SIZE - count,
			   "ro|rw|cancel|cold|disable-jump|hibernate");
	count += scnprintf(buf + count, PAGE_SIZE - count,
			   " [at-shutdown]\n");
	return count;
}

static ssize_t store_ec_reboot(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	static const struct {
		const char * const str;
		uint8_t cmd;
		uint8_t flags;
	} words[] = {
		{"cancel",       EC_REBOOT_CANCEL, 0},
		{"ro",           EC_REBOOT_JUMP_RO, 0},
		{"rw",           EC_REBOOT_JUMP_RW, 0},
		{"cold",         EC_REBOOT_COLD, 0},
		{"disable-jump", EC_REBOOT_DISABLE_JUMP, 0},
		{"hibernate",    EC_REBOOT_HIBERNATE, 0},
		{"at-shutdown",  -1, EC_REBOOT_FLAG_ON_AP_SHUTDOWN},
	};
	struct cros_ec_command *msg;
	struct ec_params_reboot_ec *param;
	int got_cmd = 0, offset = 0;
	int i;
	int ret;
	struct cros_ec_dev *ec = container_of(dev,
					      struct cros_ec_dev, class_dev);

	msg = kmalloc(sizeof(*msg) + sizeof(*param), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	param = (struct ec_params_reboot_ec *)msg->data;

	param->flags = 0;
	while (1) {
		/* Find word to start scanning */
		while (buf[offset] && isspace(buf[offset]))
			offset++;
		if (!buf[offset])
			break;

		for (i = 0; i < ARRAY_SIZE(words); i++) {
			if (!strncasecmp(words[i].str, buf+offset,
					 strlen(words[i].str))) {
				if (words[i].flags) {
					param->flags |= words[i].flags;
				} else {
					param->cmd = words[i].cmd;
					got_cmd = 1;
				}
				break;
			}
		}

		/* On to the next word, if any */
		while (buf[offset] && !isspace(buf[offset]))
			offset++;
	}

	if (!got_cmd) {
		count = -EINVAL;
		goto exit;
	}

	msg->version = 0;
	msg->command = EC_CMD_REBOOT_EC + ec->cmd_offset;
	msg->outsize = sizeof(*param);
	msg->insize = 0;
	ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
	if (ret < 0) {
		count = ret;
		goto exit;
	}
	if (msg->result != EC_RES_SUCCESS) {
		dev_dbg(ec->dev, "EC result %d\n", msg->result);
		count = -EINVAL;
	}
exit:
	kfree(msg);
	return count;
}

static ssize_t show_ec_version(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	static const char * const image_names[] = {"unknown", "RO", "RW"};
	struct ec_response_get_version *r_ver;
	struct ec_response_get_chip_info *r_chip;
	struct ec_response_board_version *r_board;
	struct cros_ec_command *msg;
	int ret;
	int count = 0;
	struct cros_ec_dev *ec = container_of(dev,
					      struct cros_ec_dev, class_dev);

	msg = kmalloc(sizeof(*msg) + EC_HOST_PARAM_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	/* Get versions. RW may change. */
	msg->version = 0;
	msg->command = EC_CMD_GET_VERSION + ec->cmd_offset;
	msg->insize = sizeof(*r_ver);
	msg->outsize = 0;
	ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
	if (ret < 0) {
		count = ret;
		goto exit;
	}
	if (msg->result != EC_RES_SUCCESS) {
		count = scnprintf(buf, PAGE_SIZE,
				  "ERROR: EC returned %d\n", msg->result);
		goto exit;
	}

	r_ver = (struct ec_response_get_version *)msg->data;
	/* Strings should be null-terminated, but let's be sure. */
	r_ver->version_string_ro[sizeof(r_ver->version_string_ro) - 1] = '\0';
	r_ver->version_string_rw[sizeof(r_ver->version_string_rw) - 1] = '\0';
	count += scnprintf(buf + count, PAGE_SIZE - count,
			   "RO version:    %s\n", r_ver->version_string_ro);
	count += scnprintf(buf + count, PAGE_SIZE - count,
			   "RW version:    %s\n", r_ver->version_string_rw);
	count += scnprintf(buf + count, PAGE_SIZE - count,
			   "Firmware copy: %s\n",
			   (r_ver->current_image < ARRAY_SIZE(image_names) ?
			    image_names[r_ver->current_image] : "?"));

	/* Get build info. */
	msg->command = EC_CMD_GET_BUILD_INFO + ec->cmd_offset;
	msg->insize = EC_HOST_PARAM_SIZE;
	ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
	if (ret < 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Build info:    XFER ERROR %d\n", ret);
	else if (msg->result != EC_RES_SUCCESS)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Build info:    EC error %d\n", msg->result);
	else {
		msg->data[sizeof(msg->data) - 1] = '\0';
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Build info:    %s\n", msg->data);
	}

	/* Get chip info. */
	msg->command = EC_CMD_GET_CHIP_INFO + ec->cmd_offset;
	msg->insize = sizeof(*r_chip);
	ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
	if (ret < 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Chip info:     XFER ERROR %d\n", ret);
	else if (msg->result != EC_RES_SUCCESS)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Chip info:     EC error %d\n", msg->result);
	else {
		r_chip = (struct ec_response_get_chip_info *)msg->data;

		r_chip->vendor[sizeof(r_chip->vendor) - 1] = '\0';
		r_chip->name[sizeof(r_chip->name) - 1] = '\0';
		r_chip->revision[sizeof(r_chip->revision) - 1] = '\0';
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Chip vendor:   %s\n", r_chip->vendor);
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Chip name:     %s\n", r_chip->name);
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Chip revision: %s\n", r_chip->revision);
	}

	/* Get board version */
	msg->command = EC_CMD_GET_BOARD_VERSION + ec->cmd_offset;
	msg->insize = sizeof(*r_board);
	ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
	if (ret < 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Board version: XFER ERROR %d\n", ret);
	else if (msg->result != EC_RES_SUCCESS)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Board version: EC error %d\n", msg->result);
	else {
		r_board = (struct ec_response_board_version *)msg->data;

		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Board version: %d\n",
				   r_board->board_version);
	}

exit:
	kfree(msg);
	return count;
}

static ssize_t show_ec_flashinfo(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ec_response_flash_info *resp;
	struct cros_ec_command *msg;
	int ret;
	struct cros_ec_dev *ec = container_of(dev,
					      struct cros_ec_dev, class_dev);

	msg = kmalloc(sizeof(*msg) + sizeof(*resp), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	/* The flash info shouldn't ever change, but ask each time anyway. */
	msg->version = 0;
	msg->command = EC_CMD_FLASH_INFO + ec->cmd_offset;
	msg->insize = sizeof(*resp);
	msg->outsize = 0;
	ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
	if (ret < 0)
		goto exit;
	if (msg->result != EC_RES_SUCCESS) {
		ret = scnprintf(buf, PAGE_SIZE,
				"ERROR: EC returned %d\n", msg->result);
		goto exit;
	}

	resp = (struct ec_response_flash_info *)msg->data;

	ret = scnprintf(buf, PAGE_SIZE,
			"FlashSize %d\nWriteSize %d\n"
			"EraseSize %d\nProtectSize %d\n",
			resp->flash_size, resp->write_block_size,
			resp->erase_block_size, resp->protect_block_size);
exit:
	kfree(msg);
	return ret;
}

/* Module initialization */

static DEVICE_ATTR(reboot, S_IWUSR | S_IRUGO, show_ec_reboot, store_ec_reboot);
static DEVICE_ATTR(version, S_IRUGO, show_ec_version, NULL);
static DEVICE_ATTR(flashinfo, S_IRUGO, show_ec_flashinfo, NULL);

static struct attribute *__ec_attrs[] = {
	&dev_attr_reboot.attr,
	&dev_attr_version.attr,
	&dev_attr_flashinfo.attr,
	NULL,
};

struct attribute_group cros_ec_attr_group = {
	.attrs = __ec_attrs,
};

