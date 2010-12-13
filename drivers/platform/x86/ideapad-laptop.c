/*
 *  ideapad_acpi.c - Lenovo IdeaPad ACPI Extras
 *
 *  Copyright © 2010 Intel Corporation
 *  Copyright © 2010 David Woodhouse <dwmw2@infradead.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <linux/rfkill.h>
#include <linux/platform_device.h>

#define IDEAPAD_DEV_CAMERA	0
#define IDEAPAD_DEV_WLAN	1
#define IDEAPAD_DEV_BLUETOOTH	2
#define IDEAPAD_DEV_3G		3
#define IDEAPAD_DEV_KILLSW	4

struct ideapad_private {
	acpi_handle handle;
	struct rfkill *rfk[5];
	struct platform_device *platform_device;
} *ideapad_priv;

static struct {
	char *name;
	int cfgbit;
	int opcode;
	int type;
} ideapad_rfk_data[] = {
	{ "ideapad_camera",	19, 0x1E, NUM_RFKILL_TYPES },
	{ "ideapad_wlan",	18, 0x15, RFKILL_TYPE_WLAN },
	{ "ideapad_bluetooth",	16, 0x17, RFKILL_TYPE_BLUETOOTH },
	{ "ideapad_3g",		17, 0x20, RFKILL_TYPE_WWAN },
	{ "ideapad_killsw",	0,  0,    RFKILL_TYPE_WLAN }
};

static bool no_bt_rfkill;
module_param(no_bt_rfkill, bool, 0444);
MODULE_PARM_DESC(no_bt_rfkill, "No rfkill for bluetooth.");

/*
 * ACPI Helpers
 */
#define IDEAPAD_EC_TIMEOUT (100) /* in ms */

static int read_method_int(acpi_handle handle, const char *method, int *val)
{
	acpi_status status;
	unsigned long long result;

	status = acpi_evaluate_integer(handle, (char *)method, NULL, &result);
	if (ACPI_FAILURE(status)) {
		*val = -1;
		return -1;
	} else {
		*val = result;
		return 0;
	}
}

static int method_vpcr(acpi_handle handle, int cmd, int *ret)
{
	acpi_status status;
	unsigned long long result;
	struct acpi_object_list params;
	union acpi_object in_obj;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = cmd;

	status = acpi_evaluate_integer(handle, "VPCR", &params, &result);

	if (ACPI_FAILURE(status)) {
		*ret = -1;
		return -1;
	} else {
		*ret = result;
		return 0;
	}
}

static int method_vpcw(acpi_handle handle, int cmd, int data)
{
	struct acpi_object_list params;
	union acpi_object in_obj[2];
	acpi_status status;

	params.count = 2;
	params.pointer = in_obj;
	in_obj[0].type = ACPI_TYPE_INTEGER;
	in_obj[0].integer.value = cmd;
	in_obj[1].type = ACPI_TYPE_INTEGER;
	in_obj[1].integer.value = data;

	status = acpi_evaluate_object(handle, "VPCW", &params, NULL);
	if (status != AE_OK)
		return -1;
	return 0;
}

static int read_ec_data(acpi_handle handle, int cmd, unsigned long *data)
{
	int val;
	unsigned long int end_jiffies;

	if (method_vpcw(handle, 1, cmd))
		return -1;

	for (end_jiffies = jiffies+(HZ)*IDEAPAD_EC_TIMEOUT/1000+1;
	     time_before(jiffies, end_jiffies);) {
		schedule();
		if (method_vpcr(handle, 1, &val))
			return -1;
		if (val == 0) {
			if (method_vpcr(handle, 0, &val))
				return -1;
			*data = val;
			return 0;
		}
	}
	pr_err("timeout in read_ec_cmd\n");
	return -1;
}

static int write_ec_cmd(acpi_handle handle, int cmd, unsigned long data)
{
	int val;
	unsigned long int end_jiffies;

	if (method_vpcw(handle, 0, data))
		return -1;
	if (method_vpcw(handle, 1, cmd))
		return -1;

	for (end_jiffies = jiffies+(HZ)*IDEAPAD_EC_TIMEOUT/1000+1;
	     time_before(jiffies, end_jiffies);) {
		schedule();
		if (method_vpcr(handle, 1, &val))
			return -1;
		if (val == 0)
			return 0;
	}
	pr_err("timeout in write_ec_cmd\n");
	return -1;
}
/* the above is ACPI helpers */

static ssize_t show_ideapad_cam(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	acpi_handle handle = priv->handle;
	unsigned long result;

	if (read_ec_data(handle, 0x1D, &result))
		return sprintf(buf, "-1\n");
	return sprintf(buf, "%lu\n", result);
}

static ssize_t store_ideapad_cam(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	acpi_handle handle = priv->handle;
	int ret, state;

	if (!count)
		return 0;
	if (sscanf(buf, "%i", &state) != 1)
		return -EINVAL;
	ret = write_ec_cmd(handle, 0x1E, state);
	if (ret < 0)
		return ret;
	return count;
}

static DEVICE_ATTR(camera_power, 0644, show_ideapad_cam, store_ideapad_cam);

static int ideapad_rfk_set(void *data, bool blocked)
{
	int device = (unsigned long)data;

	if (device == IDEAPAD_DEV_KILLSW)
		return -EINVAL;

	return write_ec_cmd(ideapad_priv->handle,
			    ideapad_rfk_data[device].opcode,
			    !blocked);
}

static struct rfkill_ops ideapad_rfk_ops = {
	.set_block = ideapad_rfk_set,
};

static void ideapad_sync_rfk_state(struct acpi_device *adevice)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);
	acpi_handle handle = priv->handle;
	unsigned long hw_blocked;
	int i;

	if (read_ec_data(handle, 0x23, &hw_blocked))
		return;
	hw_blocked = !hw_blocked;

	for (i = IDEAPAD_DEV_WLAN; i <= IDEAPAD_DEV_KILLSW; i++)
		if (priv->rfk[i])
			rfkill_set_hw_state(priv->rfk[i], hw_blocked);
}

static int ideapad_register_rfkill(struct acpi_device *adevice, int dev)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);
	int ret;
	unsigned long sw_blocked;

	if (no_bt_rfkill &&
	    (ideapad_rfk_data[dev].type == RFKILL_TYPE_BLUETOOTH)) {
		/* Force to enable bluetooth when no_bt_rfkill=1 */
		write_ec_cmd(ideapad_priv->handle,
			     ideapad_rfk_data[dev].opcode, 1);
		return 0;
	}

	priv->rfk[dev] = rfkill_alloc(ideapad_rfk_data[dev].name, &adevice->dev,
				      ideapad_rfk_data[dev].type, &ideapad_rfk_ops,
				      (void *)(long)dev);
	if (!priv->rfk[dev])
		return -ENOMEM;

	if (read_ec_data(ideapad_priv->handle, ideapad_rfk_data[dev].opcode-1,
			 &sw_blocked)) {
		rfkill_init_sw_state(priv->rfk[dev], 0);
	} else {
		sw_blocked = !sw_blocked;
		rfkill_init_sw_state(priv->rfk[dev], sw_blocked);
	}

	ret = rfkill_register(priv->rfk[dev]);
	if (ret) {
		rfkill_destroy(priv->rfk[dev]);
		return ret;
	}
	return 0;
}

static void ideapad_unregister_rfkill(struct acpi_device *adevice, int dev)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);

	if (!priv->rfk[dev])
		return;

	rfkill_unregister(priv->rfk[dev]);
	rfkill_destroy(priv->rfk[dev]);
}

/*
 * Platform device
 */
static struct attribute *ideapad_attributes[] = {
	&dev_attr_camera_power.attr,
	NULL
};

static struct attribute_group ideapad_attribute_group = {
	.attrs = ideapad_attributes
};

static int __devinit ideapad_platform_init(void)
{
	int result;

	ideapad_priv->platform_device = platform_device_alloc("ideapad", -1);
	if (!ideapad_priv->platform_device)
		return -ENOMEM;
	platform_set_drvdata(ideapad_priv->platform_device, ideapad_priv);

	result = platform_device_add(ideapad_priv->platform_device);
	if (result)
		goto fail_platform_device;

	result = sysfs_create_group(&ideapad_priv->platform_device->dev.kobj,
				    &ideapad_attribute_group);
	if (result)
		goto fail_sysfs;
	return 0;

fail_sysfs:
	platform_device_del(ideapad_priv->platform_device);
fail_platform_device:
	platform_device_put(ideapad_priv->platform_device);
	return result;
}

static void ideapad_platform_exit(void)
{
	sysfs_remove_group(&ideapad_priv->platform_device->dev.kobj,
			   &ideapad_attribute_group);
	platform_device_unregister(ideapad_priv->platform_device);
}
/* the above is platform device */

static const struct acpi_device_id ideapad_device_ids[] = {
	{ "VPC2004", 0},
	{ "", 0},
};
MODULE_DEVICE_TABLE(acpi, ideapad_device_ids);

static int ideapad_acpi_add(struct acpi_device *adevice)
{
	int ret, i, cfg;
	struct ideapad_private *priv;

	if (read_method_int(adevice->handle, "_CFG", &cfg))
		return -ENODEV;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	ideapad_priv = priv;
	priv->handle = adevice->handle;
	dev_set_drvdata(&adevice->dev, priv);

	ret = ideapad_platform_init();
	if (ret)
		goto platform_failed;

	for (i = IDEAPAD_DEV_WLAN; i < IDEAPAD_DEV_KILLSW; i++) {
		if (test_bit(ideapad_rfk_data[i].cfgbit, (unsigned long *)&cfg))
			ideapad_register_rfkill(adevice, i);
	}
	ideapad_sync_rfk_state(adevice);

	return 0;

platform_failed:
	kfree(priv);
	return ret;
}

static int ideapad_acpi_remove(struct acpi_device *adevice, int type)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);
	int i;

	for (i = IDEAPAD_DEV_WLAN; i < IDEAPAD_DEV_KILLSW; i++)
		ideapad_unregister_rfkill(adevice, i);
	ideapad_platform_exit();
	dev_set_drvdata(&adevice->dev, NULL);
	kfree(priv);

	return 0;
}

static void ideapad_acpi_notify(struct acpi_device *adevice, u32 event)
{
	acpi_handle handle = adevice->handle;
	unsigned long vpc1, vpc2, vpc_bit;

	if (read_ec_data(handle, 0x10, &vpc1))
		return;
	if (read_ec_data(handle, 0x1A, &vpc2))
		return;

	vpc1 = (vpc2 << 8) | vpc1;
	for (vpc_bit = 0; vpc_bit < 16; vpc_bit++) {
		if (test_bit(vpc_bit, &vpc1)) {
			if (vpc_bit == 9)
				ideapad_sync_rfk_state(adevice);
		}
	}
}

static struct acpi_driver ideapad_acpi_driver = {
	.name = "ideapad_acpi",
	.class = "IdeaPad",
	.ids = ideapad_device_ids,
	.ops.add = ideapad_acpi_add,
	.ops.remove = ideapad_acpi_remove,
	.ops.notify = ideapad_acpi_notify,
	.owner = THIS_MODULE,
};


static int __init ideapad_acpi_module_init(void)
{
	acpi_bus_register_driver(&ideapad_acpi_driver);

	return 0;
}


static void __exit ideapad_acpi_module_exit(void)
{
	acpi_bus_unregister_driver(&ideapad_acpi_driver);

}

MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("IdeaPad ACPI Extras");
MODULE_LICENSE("GPL");

module_init(ideapad_acpi_module_init);
module_exit(ideapad_acpi_module_exit);
