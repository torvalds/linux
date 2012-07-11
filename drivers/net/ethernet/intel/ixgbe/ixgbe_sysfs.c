/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2012 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include "ixgbe.h"
#include "ixgbe_common.h"
#include "ixgbe_type.h"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/hwmon.h>

#ifdef CONFIG_IXGBE_HWMON
/* hwmon callback functions */
static ssize_t ixgbe_hwmon_show_location(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct hwmon_attr *ixgbe_attr = container_of(attr, struct hwmon_attr,
						     dev_attr);
	return sprintf(buf, "loc%u\n",
		       ixgbe_attr->sensor->location);
}

static ssize_t ixgbe_hwmon_show_temp(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hwmon_attr *ixgbe_attr = container_of(attr, struct hwmon_attr,
						     dev_attr);
	unsigned int value;

	/* reset the temp field */
	ixgbe_attr->hw->mac.ops.get_thermal_sensor_data(ixgbe_attr->hw);

	value = ixgbe_attr->sensor->temp;

	/* display millidegree */
	value *= 1000;

	return sprintf(buf, "%u\n", value);
}

static ssize_t ixgbe_hwmon_show_cautionthresh(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hwmon_attr *ixgbe_attr = container_of(attr, struct hwmon_attr,
						     dev_attr);
	unsigned int value = ixgbe_attr->sensor->caution_thresh;

	/* display millidegree */
	value *= 1000;

	return sprintf(buf, "%u\n", value);
}

static ssize_t ixgbe_hwmon_show_maxopthresh(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hwmon_attr *ixgbe_attr = container_of(attr, struct hwmon_attr,
						     dev_attr);
	unsigned int value = ixgbe_attr->sensor->max_op_thresh;

	/* display millidegree */
	value *= 1000;

	return sprintf(buf, "%u\n", value);
}

/*
 * ixgbe_add_hwmon_attr - Create hwmon attr table for a hwmon sysfs file.
 * @ adapter: pointer to the adapter structure
 * @ offset: offset in the eeprom sensor data table
 * @ type: type of sensor data to display
 *
 * For each file we want in hwmon's sysfs interface we need a device_attribute
 * This is included in our hwmon_attr struct that contains the references to
 * the data structures we need to get the data to display.
 */
static int ixgbe_add_hwmon_attr(struct ixgbe_adapter *adapter,
				unsigned int offset, int type) {
	int rc;
	unsigned int n_attr;
	struct hwmon_attr *ixgbe_attr;

	n_attr = adapter->ixgbe_hwmon_buff.n_hwmon;
	ixgbe_attr = &adapter->ixgbe_hwmon_buff.hwmon_list[n_attr];

	switch (type) {
	case IXGBE_HWMON_TYPE_LOC:
		ixgbe_attr->dev_attr.show = ixgbe_hwmon_show_location;
		snprintf(ixgbe_attr->name, sizeof(ixgbe_attr->name),
			 "temp%u_label", offset);
		break;
	case IXGBE_HWMON_TYPE_TEMP:
		ixgbe_attr->dev_attr.show = ixgbe_hwmon_show_temp;
		snprintf(ixgbe_attr->name, sizeof(ixgbe_attr->name),
			 "temp%u_input", offset);
		break;
	case IXGBE_HWMON_TYPE_CAUTION:
		ixgbe_attr->dev_attr.show = ixgbe_hwmon_show_cautionthresh;
		snprintf(ixgbe_attr->name, sizeof(ixgbe_attr->name),
			 "temp%u_max", offset);
		break;
	case IXGBE_HWMON_TYPE_MAX:
		ixgbe_attr->dev_attr.show = ixgbe_hwmon_show_maxopthresh;
		snprintf(ixgbe_attr->name, sizeof(ixgbe_attr->name),
			 "temp%u_crit", offset);
		break;
	default:
		rc = -EPERM;
		return rc;
	}

	/* These always the same regardless of type */
	ixgbe_attr->sensor =
		&adapter->hw.mac.thermal_sensor_data.sensor[offset];
	ixgbe_attr->hw = &adapter->hw;
	ixgbe_attr->dev_attr.store = NULL;
	ixgbe_attr->dev_attr.attr.mode = S_IRUGO;
	ixgbe_attr->dev_attr.attr.name = ixgbe_attr->name;

	rc = device_create_file(&adapter->pdev->dev,
				&ixgbe_attr->dev_attr);

	if (rc == 0)
		++adapter->ixgbe_hwmon_buff.n_hwmon;

	return rc;
}

static void ixgbe_sysfs_del_adapter(struct ixgbe_adapter *adapter)
{
	int i;

	if (adapter == NULL)
		return;

	for (i = 0; i < adapter->ixgbe_hwmon_buff.n_hwmon; i++) {
		device_remove_file(&adapter->pdev->dev,
			   &adapter->ixgbe_hwmon_buff.hwmon_list[i].dev_attr);
	}

	kfree(adapter->ixgbe_hwmon_buff.hwmon_list);

	if (adapter->ixgbe_hwmon_buff.device)
		hwmon_device_unregister(adapter->ixgbe_hwmon_buff.device);
}

/* called from ixgbe_main.c */
void ixgbe_sysfs_exit(struct ixgbe_adapter *adapter)
{
	ixgbe_sysfs_del_adapter(adapter);
}

/* called from ixgbe_main.c */
int ixgbe_sysfs_init(struct ixgbe_adapter *adapter)
{
	struct hwmon_buff *ixgbe_hwmon = &adapter->ixgbe_hwmon_buff;
	unsigned int i;
	int n_attrs;
	int rc = 0;

	/* If this method isn't defined we don't support thermals */
	if (adapter->hw.mac.ops.init_thermal_sensor_thresh == NULL) {
		goto exit;
	}

	/* Don't create thermal hwmon interface if no sensors present */
	if (adapter->hw.mac.ops.init_thermal_sensor_thresh(&adapter->hw))
		goto exit;

	/*
	 * Allocation space for max attributs
	 * max num sensors * values (loc, temp, max, caution)
	 */
	n_attrs = IXGBE_MAX_SENSORS * 4;
	ixgbe_hwmon->hwmon_list = kcalloc(n_attrs, sizeof(struct hwmon_attr),
					  GFP_KERNEL);
	if (!ixgbe_hwmon->hwmon_list) {
		rc = -ENOMEM;
		goto err;
	}

	ixgbe_hwmon->device = hwmon_device_register(&adapter->pdev->dev);
	if (IS_ERR(ixgbe_hwmon->device)) {
		rc = PTR_ERR(ixgbe_hwmon->device);
		goto err;
	}

	for (i = 0; i < IXGBE_MAX_SENSORS; i++) {
		/*
		 * Only create hwmon sysfs entries for sensors that have
		 * meaningful data for.
		 */
		if (adapter->hw.mac.thermal_sensor_data.sensor[i].location == 0)
			continue;

		/* Bail if any hwmon attr struct fails to initialize */
		rc = ixgbe_add_hwmon_attr(adapter, i, IXGBE_HWMON_TYPE_CAUTION);
		rc |= ixgbe_add_hwmon_attr(adapter, i, IXGBE_HWMON_TYPE_LOC);
		rc |= ixgbe_add_hwmon_attr(adapter, i, IXGBE_HWMON_TYPE_TEMP);
		rc |= ixgbe_add_hwmon_attr(adapter, i, IXGBE_HWMON_TYPE_MAX);
		if (rc)
			goto err;
	}

	goto exit;

err:
	ixgbe_sysfs_del_adapter(adapter);
exit:
	return rc;
}
#endif /* CONFIG_IXGBE_HWMON */

