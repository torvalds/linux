// SPDX-License-Identifier: GPL-2.0

/* Driver for ETAS GmbH ES58X USB CAN(-FD) Bus Interfaces.
 *
 * File es58x_devlink.c: report the product information using devlink.
 *
 * Copyright (c) 2022 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/usb.h>
#include <net/devlink.h>

#include "es58x_core.h"

/* USB descriptor index containing the product information string. */
#define ES58X_PROD_INFO_IDX 6

/**
 * es58x_parse_sw_version() - Extract boot loader or firmware version.
 * @es58x_dev: ES58X device.
 * @prod_info: USB custom string returned by the device.
 * @prefix: Select which information should be parsed. Set it to "FW"
 *	to parse the firmware version or to "BL" to parse the
 *	bootloader version.
 *
 * The @prod_info string contains the firmware and the bootloader
 * version number all prefixed by a magic string and concatenated with
 * other numbers. Depending on the device, the firmware (bootloader)
 * format is either "FW_Vxx.xx.xx" ("BL_Vxx.xx.xx") or "FW:xx.xx.xx"
 * ("BL:xx.xx.xx") where 'x' represents a digit. @prod_info must
 * contains the common part of those prefixes: "FW" or "BL".
 *
 * Parse @prod_info and store the version number in
 * &es58x_dev.firmware_version or &es58x_dev.bootloader_version
 * according to @prefix value.
 *
 * Return: zero on success, -EINVAL if @prefix contains an invalid
 *	value and -EBADMSG if @prod_info could not be parsed.
 */
static int es58x_parse_sw_version(struct es58x_device *es58x_dev,
				  const char *prod_info, const char *prefix)
{
	struct es58x_sw_version *version;
	int major, minor, revision;

	if (!strcmp(prefix, "FW"))
		version = &es58x_dev->firmware_version;
	else if (!strcmp(prefix, "BL"))
		version = &es58x_dev->bootloader_version;
	else
		return -EINVAL;

	/* Go to prefix */
	prod_info = strstr(prod_info, prefix);
	if (!prod_info)
		return -EBADMSG;
	/* Go to beginning of the version number */
	while (!isdigit(*prod_info)) {
		prod_info++;
		if (!*prod_info)
			return -EBADMSG;
	}

	if (sscanf(prod_info, "%2u.%2u.%2u", &major, &minor, &revision) != 3)
		return -EBADMSG;

	version->major = major;
	version->minor = minor;
	version->revision = revision;

	return 0;
}

/**
 * es58x_parse_hw_rev() - Extract hardware revision number.
 * @es58x_dev: ES58X device.
 * @prod_info: USB custom string returned by the device.
 *
 * @prod_info contains the hardware revision prefixed by a magic
 * string and conquenated together with other numbers. Depending on
 * the device, the hardware revision format is either
 * "HW_VER:axxx/xxx" or "HR:axxx/xxx" where 'a' represents a letter
 * and 'x' a digit.
 *
 * Parse @prod_info and store the hardware revision number in
 * &es58x_dev.hardware_revision.
 *
 * Return: zero on success, -EBADMSG if @prod_info could not be
 *	parsed.
 */
static int es58x_parse_hw_rev(struct es58x_device *es58x_dev,
			      const char *prod_info)
{
	char letter;
	int major, minor;

	/* The only occurrence of 'H' is in the hardware revision prefix. */
	prod_info = strchr(prod_info, 'H');
	if (!prod_info)
		return -EBADMSG;
	/* Go to beginning of the hardware revision */
	prod_info = strchr(prod_info, ':');
	if (!prod_info)
		return -EBADMSG;
	prod_info++;

	if (sscanf(prod_info, "%c%3u/%3u", &letter, &major, &minor) != 3)
		return -EBADMSG;

	es58x_dev->hardware_revision.letter = letter;
	es58x_dev->hardware_revision.major = major;
	es58x_dev->hardware_revision.minor = minor;

	return 0;
}

/**
 * es58x_parse_product_info() - Parse the ES58x product information
 *	string.
 * @es58x_dev: ES58X device.
 *
 * Retrieve the product information string and parse it to extract the
 * firmware version, the bootloader version and the hardware
 * revision.
 *
 * If the function fails, set the version or revision to an invalid
 * value and emit an informal message. Continue probing because the
 * product information is not critical for the driver to operate.
 */
void es58x_parse_product_info(struct es58x_device *es58x_dev)
{
	static const struct es58x_sw_version sw_version_not_set = {
		.major = -1,
		.minor = -1,
		.revision = -1,
	};
	static const struct es58x_hw_revision hw_revision_not_set = {
		.letter = '\0',
		.major = -1,
		.minor = -1,
	};
	char *prod_info;

	es58x_dev->firmware_version = sw_version_not_set;
	es58x_dev->bootloader_version = sw_version_not_set;
	es58x_dev->hardware_revision = hw_revision_not_set;

	prod_info = usb_cache_string(es58x_dev->udev, ES58X_PROD_INFO_IDX);
	if (!prod_info) {
		dev_warn(es58x_dev->dev,
			 "could not retrieve the product info string\n");
		return;
	}

	if (es58x_parse_sw_version(es58x_dev, prod_info, "FW") ||
	    es58x_parse_sw_version(es58x_dev, prod_info, "BL") ||
	    es58x_parse_hw_rev(es58x_dev, prod_info))
		dev_info(es58x_dev->dev,
			 "could not parse product info: '%s'\n", prod_info);

	kfree(prod_info);
}

/**
 * es58x_sw_version_is_valid() - Check if the version is a valid number.
 * @sw_ver: Version number of either the firmware or the bootloader.
 *
 * If any of the software version sub-numbers do not fit on two
 * digits, the version is invalid, most probably because the product
 * string could not be parsed.
 *
 * Return: @true if the software version is valid, @false otherwise.
 */
static inline bool es58x_sw_version_is_valid(struct es58x_sw_version *sw_ver)
{
	return sw_ver->major < 100 && sw_ver->minor < 100 &&
		sw_ver->revision < 100;
}

/**
 * es58x_hw_revision_is_valid() - Check if the revision is a valid number.
 * @hw_rev: Revision number of the hardware.
 *
 * If &es58x_hw_revision.letter is not a alphanumeric character or if
 * any of the hardware revision sub-numbers do not fit on three
 * digits, the revision is invalid, most probably because the product
 * string could not be parsed.
 *
 * Return: @true if the hardware revision is valid, @false otherwise.
 */
static inline bool es58x_hw_revision_is_valid(struct es58x_hw_revision *hw_rev)
{
	return isalnum(hw_rev->letter) && hw_rev->major < 1000 &&
		hw_rev->minor < 1000;
}

/**
 * es58x_devlink_info_get() - Report the product information.
 * @devlink: Devlink.
 * @req: skb wrapper where to put requested information.
 * @extack: Unused.
 *
 * Report the firmware version, the bootloader version, the hardware
 * revision and the serial number through netlink.
 *
 * Return: zero on success, errno when any error occurs.
 */
static int es58x_devlink_info_get(struct devlink *devlink,
				  struct devlink_info_req *req,
				  struct netlink_ext_ack *extack)
{
	struct es58x_device *es58x_dev = devlink_priv(devlink);
	struct es58x_sw_version *fw_ver = &es58x_dev->firmware_version;
	struct es58x_sw_version *bl_ver = &es58x_dev->bootloader_version;
	struct es58x_hw_revision *hw_rev = &es58x_dev->hardware_revision;
	char buf[MAX(sizeof("xx.xx.xx"), sizeof("axxx/xxx"))];
	int ret = 0;

	if (es58x_sw_version_is_valid(fw_ver)) {
		snprintf(buf, sizeof(buf), "%02u.%02u.%02u",
			 fw_ver->major, fw_ver->minor, fw_ver->revision);
		ret = devlink_info_version_running_put(req,
						       DEVLINK_INFO_VERSION_GENERIC_FW,
						       buf);
		if (ret)
			return ret;
	}

	if (es58x_sw_version_is_valid(bl_ver)) {
		snprintf(buf, sizeof(buf), "%02u.%02u.%02u",
			 bl_ver->major, bl_ver->minor, bl_ver->revision);
		ret = devlink_info_version_running_put(req,
						       DEVLINK_INFO_VERSION_GENERIC_FW_BOOTLOADER,
						       buf);
		if (ret)
			return ret;
	}

	if (es58x_hw_revision_is_valid(hw_rev)) {
		snprintf(buf, sizeof(buf), "%c%03u/%03u",
			 hw_rev->letter, hw_rev->major, hw_rev->minor);
		ret = devlink_info_version_fixed_put(req,
						     DEVLINK_INFO_VERSION_GENERIC_BOARD_REV,
						     buf);
		if (ret)
			return ret;
	}

	return devlink_info_serial_number_put(req, es58x_dev->udev->serial);
}

const struct devlink_ops es58x_dl_ops = {
	.info_get = es58x_devlink_info_get,
};
