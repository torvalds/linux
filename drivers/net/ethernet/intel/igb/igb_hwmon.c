/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2013 Intel Corporation.

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

#include "igb.h"
#include "e1000_82575.h"
#include "e1000_hw.h"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/hwmon.h>
#include <linux/pci.h>

#ifdef CONFIG_IGB_HWMON
struct i2c_board_info i350_sensor_info = {
	I2C_BOARD_INFO("i350bb", (0Xf8 >> 1)),
};

/* hwmon callback functions */
static ssize_t igb_hwmon_show_location(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct hwmon_attr *igb_attr = container_of(attr, struct hwmon_attr,
						     dev_attr);
	return sprintf(buf, "loc%u\n",
		       igb_attr->sensor->location);
}

static ssize_t igb_hwmon_show_temp(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hwmon_attr *igb_attr = container_of(attr, struct hwmon_attr,
						     dev_attr);
	unsigned int value;

	/* reset the temp field */
	igb_attr->hw->mac.ops.get_thermal_sensor_data(igb_attr->hw);

	value = igb_attr->sensor->temp;

	/* display millidegree */
	value *= 1000;

	return sprintf(buf, "%u\n", value);
}

static ssize_t igb_hwmon_show_cautionthresh(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hwmon_attr *igb_attr = container_of(attr, struct hwmon_attr,
						     dev_attr);
	unsigned int value = igb_attr->sensor->caution_thresh;

	/* display millidegree */
	value *= 1000;

	return sprintf(buf, "%u\n", value);
}

static ssize_t igb_hwmon_show_maxopthresh(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hwmon_attr *igb_attr = container_of(attr, struct hwmon_attr,
						     dev_attr);
	unsigned int value = igb_attr->sensor->max_op_thresh;

	/* display millidegree */
	value *= 1000;

	return sprintf(buf, "%u\n", value);
}

/* igb_add_hwmon_attr - Create hwmon attr table for a hwmon sysfs file.
 * @ adapter: pointer to the adapter structure
 * @ offset: offset in the eeprom sensor data table
 * @ type: type of sensor data to display
 *
 * For each file we want in hwmon's sysfs interface we need a device_attribute
 * This is included in our hwmon_attr struct that contains the references to
 * the data structures we need to get the data to display.
 */
static int igb_add_hwmon_attr(struct igb_adapter *adapter,
				unsigned int offset, int type) {
	int rc;
	unsigned int n_attr;
	struct hwmon_attr *igb_attr;

	n_attr = adapter->igb_hwmon_buff.n_hwmon;
	igb_attr = &adapter->igb_hwmon_buff.hwmon_list[n_attr];

	switch (type) {
	case IGB_HWMON_TYPE_LOC:
		igb_attr->dev_attr.show = igb_hwmon_show_location;
		snprintf(igb_attr->name, sizeof(igb_attr->name),
			 "temp%u_label", offset);
		break;
	case IGB_HWMON_TYPE_TEMP:
		igb_attr->dev_attr.show = igb_hwmon_show_temp;
		snprintf(igb_attr->name, sizeof(igb_attr->name),
			 "temp%u_input", offset);
		break;
	case IGB_HWMON_TYPE_CAUTION:
		igb_attr->dev_attr.show = igb_hwmon_show_cautionthresh;
		snprintf(igb_attr->name, sizeof(igb_attr->name),
			 "temp%u_max", offset);
		break;
	case IGB_HWMON_TYPE_MAX:
		igb_attr->dev_attr.show = igb_hwmon_show_maxopthresh;
		snprintf(igb_attr->name, sizeof(igb_attr->name),
			 "temp%u_crit", offset);
		break;
	default:
		rc = -EPERM;
		return rc;
	}

	/* These always the same regardless of type */
	igb_attr->sensor =
		&adapter->hw.mac.thermal_sensor_data.sensor[offset];
	igb_attr->hw = &adapter->hw;
	igb_attr->dev_attr.store = NULL;
	igb_attr->dev_attr.attr.mode = S_IRUGO;
	igb_attr->dev_attr.attr.name = igb_attr->name;
	sysfs_attr_init(&igb_attr->dev_attr.attr);
	rc = device_create_file(&adapter->pdev->dev,
				&igb_attr->dev_attr);
	if (rc == 0)
		++adapter->igb_hwmon_buff.n_hwmon;

	return rc;
}

static void igb_sysfs_del_adapter(struct igb_adapter *adapter)
{
	int i;

	if (adapter == NULL)
		return;

	for (i = 0; i < adapter->igb_hwmon_buff.n_hwmon; i++) {
		device_remove_file(&adapter->pdev->dev,
			   &adapter->igb_hwmon_buff.hwmon_list[i].dev_attr);
	}

	kfree(adapter->igb_hwmon_buff.hwmon_list);

	if (adapter->igb_hwmon_buff.device)
		hwmon_device_unregister(adapter->igb_hwmon_buff.device);
}

/* called from igb_main.c */
void igb_sysfs_exit(struct igb_adapter *adapter)
{
	igb_sysfs_del_adapter(adapter);
}

/* called from igb_main.c */
int igb_sysfs_init(struct igb_adapter *adapter)
{
	struct hwmon_buff *igb_hwmon = &adapter->igb_hwmon_buff;
	unsigned int i;
	int n_attrs;
	int rc = 0;
	struct i2c_client *client = NULL;

	/* If this method isn't defined we don't support thermals */
	if (adapter->hw.mac.ops.init_thermal_sensor_thresh == NULL)
		goto exit;

	/* Don't create thermal hwmon interface if no sensors present */
	rc = (adapter->hw.mac.ops.init_thermal_sensor_thresh(&adapter->hw));
		if (rc)
			goto exit;

	/* init i2c_client */
	client = i2c_new_device(&adapter->i2c_adap, &i350_sensor_info);
	if (client == NULL) {
		dev_info(&adapter->pdev->dev,
			"Failed to create new i2c device..\n");
		goto exit;
	}
	adapter->i2c_client = client;

	/* Allocation space for max attributes
	 * max num sensors * values (loc, temp, max, caution)
	 */
	n_attrs = E1000_MAX_SENSORS * 4;
	igb_hwmon->hwmon_list = kcalloc(n_attrs, sizeof(struct hwmon_attr),
					  GFP_KERNEL);
	if (!igb_hwmon->hwmon_list) {
		rc = -ENOMEM;
		goto err;
	}

	igb_hwmon->device = hwmon_device_register(&adapter->pdev->dev);
	if (IS_ERR(igb_hwmon->device)) {
		rc = PTR_ERR(igb_hwmon->device);
		goto err;
	}

	for (i = 0; i < E1000_MAX_SENSORS; i++) {

		/* Only create hwmon sysfs entries for sensors that have
		 * meaningful data.
		 */
		if (adapter->hw.mac.thermal_sensor_data.sensor[i].location == 0)
			continue;

		/* Bail if any hwmon attr struct fails to initialize */
		rc = igb_add_hwmon_attr(adapter, i, IGB_HWMON_TYPE_CAUTION);
		rc |= igb_add_hwmon_attr(adapter, i, IGB_HWMON_TYPE_LOC);
		rc |= igb_add_hwmon_attr(adapter, i, IGB_HWMON_TYPE_TEMP);
		rc |= igb_add_hwmon_attr(adapter, i, IGB_HWMON_TYPE_MAX);
		if (rc)
			goto err;
	}

	goto exit;

err:
	igb_sysfs_del_adapter(adapter);
exit:
	return rc;
}
#endif
