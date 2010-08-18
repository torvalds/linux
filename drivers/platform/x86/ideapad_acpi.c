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

#define IDEAPAD_DEV_CAMERA	0
#define IDEAPAD_DEV_WLAN	1
#define IDEAPAD_DEV_BLUETOOTH	2
#define IDEAPAD_DEV_3G		3
#define IDEAPAD_DEV_KILLSW	4

struct ideapad_private {
	struct rfkill *rfk[5];
};

static struct {
	char *name;
	int type;
} ideapad_rfk_data[] = {
	/* camera has no rfkill */
	{ "ideapad_wlan",	RFKILL_TYPE_WLAN },
	{ "ideapad_bluetooth",	RFKILL_TYPE_BLUETOOTH },
	{ "ideapad_3g",		RFKILL_TYPE_WWAN },
	{ "ideapad_killsw",	RFKILL_TYPE_WLAN }
};

static int ideapad_dev_exists(int device)
{
	acpi_status status;
	union acpi_object in_param;
	struct acpi_object_list input = { 1, &in_param };
	struct acpi_buffer output;
	union acpi_object out_obj;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	in_param.type = ACPI_TYPE_INTEGER;
	in_param.integer.value = device + 1;

	status = acpi_evaluate_object(NULL, "\\_SB_.DECN", &input, &output);
	if (ACPI_FAILURE(status)) {
		printk(KERN_WARNING "IdeaPAD \\_SB_.DECN method failed %d. Is this an IdeaPAD?\n", status);
		return -ENODEV;
	}
	if (out_obj.type != ACPI_TYPE_INTEGER) {
		printk(KERN_WARNING "IdeaPAD \\_SB_.DECN method returned unexpected type\n");
		return -ENODEV;
	}
	return out_obj.integer.value;
}

static int ideapad_dev_get_state(int device)
{
	acpi_status status;
	union acpi_object in_param;
	struct acpi_object_list input = { 1, &in_param };
	struct acpi_buffer output;
	union acpi_object out_obj;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	in_param.type = ACPI_TYPE_INTEGER;
	in_param.integer.value = device + 1;

	status = acpi_evaluate_object(NULL, "\\_SB_.GECN", &input, &output);
	if (ACPI_FAILURE(status)) {
		printk(KERN_WARNING "IdeaPAD \\_SB_.GECN method failed %d\n", status);
		return -ENODEV;
	}
	if (out_obj.type != ACPI_TYPE_INTEGER) {
		printk(KERN_WARNING "IdeaPAD \\_SB_.GECN method returned unexpected type\n");
		return -ENODEV;
	}
	return out_obj.integer.value;
}

static int ideapad_dev_set_state(int device, int state)
{
	acpi_status status;
	union acpi_object in_params[2];
	struct acpi_object_list input = { 2, in_params };

	in_params[0].type = ACPI_TYPE_INTEGER;
	in_params[0].integer.value = device + 1;
	in_params[1].type = ACPI_TYPE_INTEGER;
	in_params[1].integer.value = state;

	status = acpi_evaluate_object(NULL, "\\_SB_.SECN", &input, NULL);
	if (ACPI_FAILURE(status)) {
		printk(KERN_WARNING "IdeaPAD \\_SB_.SECN method failed %d\n", status);
		return -ENODEV;
	}
	return 0;
}
static ssize_t show_ideapad_cam(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int state = ideapad_dev_get_state(IDEAPAD_DEV_CAMERA);
	if (state < 0)
		return state;

	return sprintf(buf, "%d\n", state);
}

static ssize_t store_ideapad_cam(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int ret, state;

	if (!count)
		return 0;
	if (sscanf(buf, "%i", &state) != 1)
		return -EINVAL;
	ret = ideapad_dev_set_state(IDEAPAD_DEV_CAMERA, !!state);
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
	return ideapad_dev_set_state(device, !blocked);
}

static struct rfkill_ops ideapad_rfk_ops = {
	.set_block = ideapad_rfk_set,
};

static void ideapad_sync_rfk_state(struct acpi_device *adevice)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);
	int hw_blocked = !ideapad_dev_get_state(IDEAPAD_DEV_KILLSW);
	int i;

	rfkill_set_hw_state(priv->rfk[IDEAPAD_DEV_KILLSW], hw_blocked);
	for (i = IDEAPAD_DEV_WLAN; i < IDEAPAD_DEV_KILLSW; i++)
		if (priv->rfk[i])
			rfkill_set_hw_state(priv->rfk[i], hw_blocked);
	if (hw_blocked)
		return;

	for (i = IDEAPAD_DEV_WLAN; i < IDEAPAD_DEV_KILLSW; i++)
		if (priv->rfk[i])
			rfkill_set_sw_state(priv->rfk[i], !ideapad_dev_get_state(i));
}

static int ideapad_register_rfkill(struct acpi_device *adevice, int dev)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);
	int ret;

	priv->rfk[dev] = rfkill_alloc(ideapad_rfk_data[dev-1].name, &adevice->dev,
				      ideapad_rfk_data[dev-1].type, &ideapad_rfk_ops,
				      (void *)(long)dev);
	if (!priv->rfk[dev])
		return -ENOMEM;

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

static const struct acpi_device_id ideapad_device_ids[] = {
	{ "VPC2004", 0},
	{ "", 0},
};
MODULE_DEVICE_TABLE(acpi, ideapad_device_ids);

static int ideapad_acpi_add(struct acpi_device *adevice)
{
	int i;
	int devs_present[5];
	struct ideapad_private *priv;

	for (i = IDEAPAD_DEV_CAMERA; i < IDEAPAD_DEV_KILLSW; i++) {
		devs_present[i] = ideapad_dev_exists(i);
		if (devs_present[i] < 0)
			return devs_present[i];
	}

	/* The hardware switch is always present */
	devs_present[IDEAPAD_DEV_KILLSW] = 1;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (devs_present[IDEAPAD_DEV_CAMERA]) {
		int ret = device_create_file(&adevice->dev, &dev_attr_camera_power);
		if (ret) {
			kfree(priv);
			return ret;
		}
	}

	dev_set_drvdata(&adevice->dev, priv);
	for (i = IDEAPAD_DEV_WLAN; i <= IDEAPAD_DEV_KILLSW; i++) {
		if (!devs_present[i])
			continue;

		ideapad_register_rfkill(adevice, i);
	}
	ideapad_sync_rfk_state(adevice);
	return 0;
}

static int ideapad_acpi_remove(struct acpi_device *adevice, int type)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);
	int i;

	device_remove_file(&adevice->dev, &dev_attr_camera_power);

	for (i = IDEAPAD_DEV_WLAN; i <= IDEAPAD_DEV_KILLSW; i++)
		ideapad_unregister_rfkill(adevice, i);

	dev_set_drvdata(&adevice->dev, NULL);
	kfree(priv);
	return 0;
}

static void ideapad_acpi_notify(struct acpi_device *adevice, u32 event)
{
	ideapad_sync_rfk_state(adevice);
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
