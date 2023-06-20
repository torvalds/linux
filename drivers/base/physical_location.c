// SPDX-License-Identifier: GPL-2.0
/*
 * Device physical location support
 *
 * Author: Won Chung <wonchung@google.com>
 */

#include <linux/acpi.h>
#include <linux/sysfs.h>

#include "physical_location.h"

bool dev_add_physical_location(struct device *dev)
{
	struct acpi_pld_info *pld;
	acpi_status status;

	if (!has_acpi_companion(dev))
		return false;

	status = acpi_get_physical_device_location(ACPI_HANDLE(dev), &pld);
	if (ACPI_FAILURE(status))
		return false;

	dev->physical_location =
		kzalloc(sizeof(*dev->physical_location), GFP_KERNEL);
	if (!dev->physical_location)
		return false;
	dev->physical_location->panel = pld->panel;
	dev->physical_location->vertical_position = pld->vertical_position;
	dev->physical_location->horizontal_position = pld->horizontal_position;
	dev->physical_location->dock = pld->dock;
	dev->physical_location->lid = pld->lid;

	ACPI_FREE(pld);
	return true;
}

static ssize_t panel_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	const char *panel;

	switch (dev->physical_location->panel) {
	case DEVICE_PANEL_TOP:
		panel = "top";
		break;
	case DEVICE_PANEL_BOTTOM:
		panel = "bottom";
		break;
	case DEVICE_PANEL_LEFT:
		panel = "left";
		break;
	case DEVICE_PANEL_RIGHT:
		panel = "right";
		break;
	case DEVICE_PANEL_FRONT:
		panel = "front";
		break;
	case DEVICE_PANEL_BACK:
		panel = "back";
		break;
	default:
		panel = "unknown";
	}
	return sysfs_emit(buf, "%s\n", panel);
}
static DEVICE_ATTR_RO(panel);

static ssize_t vertical_position_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	const char *vertical_position;

	switch (dev->physical_location->vertical_position) {
	case DEVICE_VERT_POS_UPPER:
		vertical_position = "upper";
		break;
	case DEVICE_VERT_POS_CENTER:
		vertical_position = "center";
		break;
	case DEVICE_VERT_POS_LOWER:
		vertical_position = "lower";
		break;
	default:
		vertical_position = "unknown";
	}
	return sysfs_emit(buf, "%s\n", vertical_position);
}
static DEVICE_ATTR_RO(vertical_position);

static ssize_t horizontal_position_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	const char *horizontal_position;

	switch (dev->physical_location->horizontal_position) {
	case DEVICE_HORI_POS_LEFT:
		horizontal_position = "left";
		break;
	case DEVICE_HORI_POS_CENTER:
		horizontal_position = "center";
		break;
	case DEVICE_HORI_POS_RIGHT:
		horizontal_position = "right";
		break;
	default:
		horizontal_position = "unknown";
	}
	return sysfs_emit(buf, "%s\n", horizontal_position);
}
static DEVICE_ATTR_RO(horizontal_position);

static ssize_t dock_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	return sysfs_emit(buf, "%s\n",
		dev->physical_location->dock ? "yes" : "no");
}
static DEVICE_ATTR_RO(dock);

static ssize_t lid_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	return sysfs_emit(buf, "%s\n",
		dev->physical_location->lid ? "yes" : "no");
}
static DEVICE_ATTR_RO(lid);

static struct attribute *dev_attr_physical_location[] = {
	&dev_attr_panel.attr,
	&dev_attr_vertical_position.attr,
	&dev_attr_horizontal_position.attr,
	&dev_attr_dock.attr,
	&dev_attr_lid.attr,
	NULL,
};

const struct attribute_group dev_attr_physical_location_group = {
	.name = "physical_location",
	.attrs = dev_attr_physical_location,
};

