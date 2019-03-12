/*
    Dell Airplane Mode Switch driver
    Copyright (C) 2014-2015  Pali Rohár <pali.rohar@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/rfkill.h>
#include <linux/input.h>

#include "dell-rbtn.h"

enum rbtn_type {
	RBTN_UNKNOWN,
	RBTN_TOGGLE,
	RBTN_SLIDER,
};

struct rbtn_data {
	enum rbtn_type type;
	struct rfkill *rfkill;
	struct input_dev *input_dev;
	bool suspended;
};


/*
 * acpi functions
 */

static enum rbtn_type rbtn_check(struct acpi_device *device)
{
	unsigned long long output;
	acpi_status status;

	status = acpi_evaluate_integer(device->handle, "CRBT", NULL, &output);
	if (ACPI_FAILURE(status))
		return RBTN_UNKNOWN;

	switch (output) {
	case 0:
	case 1:
		return RBTN_TOGGLE;
	case 2:
	case 3:
		return RBTN_SLIDER;
	default:
		return RBTN_UNKNOWN;
	}
}

static int rbtn_get(struct acpi_device *device)
{
	unsigned long long output;
	acpi_status status;

	status = acpi_evaluate_integer(device->handle, "GRBT", NULL, &output);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	return !output;
}

static int rbtn_acquire(struct acpi_device *device, bool enable)
{
	struct acpi_object_list input;
	union acpi_object param;
	acpi_status status;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = enable;
	input.count = 1;
	input.pointer = &param;

	status = acpi_evaluate_object(device->handle, "ARBT", &input, NULL);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	return 0;
}


/*
 * rfkill device
 */

static void rbtn_rfkill_query(struct rfkill *rfkill, void *data)
{
	struct acpi_device *device = data;
	int state;

	state = rbtn_get(device);
	if (state < 0)
		return;

	rfkill_set_states(rfkill, state, state);
}

static int rbtn_rfkill_set_block(void *data, bool blocked)
{
	/* NOTE: setting soft rfkill state is not supported */
	return -EINVAL;
}

static const struct rfkill_ops rbtn_ops = {
	.query = rbtn_rfkill_query,
	.set_block = rbtn_rfkill_set_block,
};

static int rbtn_rfkill_init(struct acpi_device *device)
{
	struct rbtn_data *rbtn_data = device->driver_data;
	int ret;

	if (rbtn_data->rfkill)
		return 0;

	/*
	 * NOTE: rbtn controls all radio devices, not only WLAN
	 *       but rfkill interface does not support "ANY" type
	 *       so "WLAN" type is used
	 */
	rbtn_data->rfkill = rfkill_alloc("dell-rbtn", &device->dev,
					 RFKILL_TYPE_WLAN, &rbtn_ops, device);
	if (!rbtn_data->rfkill)
		return -ENOMEM;

	ret = rfkill_register(rbtn_data->rfkill);
	if (ret) {
		rfkill_destroy(rbtn_data->rfkill);
		rbtn_data->rfkill = NULL;
		return ret;
	}

	return 0;
}

static void rbtn_rfkill_exit(struct acpi_device *device)
{
	struct rbtn_data *rbtn_data = device->driver_data;

	if (!rbtn_data->rfkill)
		return;

	rfkill_unregister(rbtn_data->rfkill);
	rfkill_destroy(rbtn_data->rfkill);
	rbtn_data->rfkill = NULL;
}

static void rbtn_rfkill_event(struct acpi_device *device)
{
	struct rbtn_data *rbtn_data = device->driver_data;

	if (rbtn_data->rfkill)
		rbtn_rfkill_query(rbtn_data->rfkill, device);
}


/*
 * input device
 */

static int rbtn_input_init(struct rbtn_data *rbtn_data)
{
	int ret;

	rbtn_data->input_dev = input_allocate_device();
	if (!rbtn_data->input_dev)
		return -ENOMEM;

	rbtn_data->input_dev->name = "DELL Wireless hotkeys";
	rbtn_data->input_dev->phys = "dellabce/input0";
	rbtn_data->input_dev->id.bustype = BUS_HOST;
	rbtn_data->input_dev->evbit[0] = BIT(EV_KEY);
	set_bit(KEY_RFKILL, rbtn_data->input_dev->keybit);

	ret = input_register_device(rbtn_data->input_dev);
	if (ret) {
		input_free_device(rbtn_data->input_dev);
		rbtn_data->input_dev = NULL;
		return ret;
	}

	return 0;
}

static void rbtn_input_exit(struct rbtn_data *rbtn_data)
{
	input_unregister_device(rbtn_data->input_dev);
	rbtn_data->input_dev = NULL;
}

static void rbtn_input_event(struct rbtn_data *rbtn_data)
{
	input_report_key(rbtn_data->input_dev, KEY_RFKILL, 1);
	input_sync(rbtn_data->input_dev);
	input_report_key(rbtn_data->input_dev, KEY_RFKILL, 0);
	input_sync(rbtn_data->input_dev);
}


/*
 * acpi driver
 */

static int rbtn_add(struct acpi_device *device);
static int rbtn_remove(struct acpi_device *device);
static void rbtn_notify(struct acpi_device *device, u32 event);

static const struct acpi_device_id rbtn_ids[] = {
	{ "DELRBTN", 0 },
	{ "DELLABCE", 0 },

	/*
	 * This driver can also handle the "DELLABC6" device that
	 * appears on the XPS 13 9350, but that device is disabled by
	 * the DSDT unless booted with acpi_osi="!Windows 2012"
	 * acpi_osi="!Windows 2013".
	 *
	 * According to Mario at Dell:
	 *
	 *  DELLABC6 is a custom interface that was created solely to
	 *  have airplane mode support for Windows 7.  For Windows 10
	 *  the proper interface is to use that which is handled by
	 *  intel-hid. A OEM airplane mode driver is not used.
	 *
	 *  Since the kernel doesn't identify as Windows 7 it would be
	 *  incorrect to do attempt to use that interface.
	 *
	 * Even if we override _OSI and bind to DELLABC6, we end up with
	 * inconsistent behavior in which userspace can get out of sync
	 * with the rfkill state as it conflicts with events from
	 * intel-hid.
	 *
	 * The upshot is that it is better to just ignore DELLABC6
	 * devices.
	 */

	{ "", 0 },
};

#ifdef CONFIG_PM_SLEEP
static void ACPI_SYSTEM_XFACE rbtn_clear_suspended_flag(void *context)
{
	struct rbtn_data *rbtn_data = context;

	rbtn_data->suspended = false;
}

static int rbtn_suspend(struct device *dev)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct rbtn_data *rbtn_data = acpi_driver_data(device);

	rbtn_data->suspended = true;

	return 0;
}

static int rbtn_resume(struct device *dev)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct rbtn_data *rbtn_data = acpi_driver_data(device);
	acpi_status status;

	/*
	 * Upon resume, some BIOSes send an ACPI notification thet triggers
	 * an unwanted input event. In order to ignore it, we use a flag
	 * that we set at suspend and clear once we have received the extra
	 * ACPI notification. Since ACPI notifications are delivered
	 * asynchronously to drivers, we clear the flag from the workqueue
	 * used to deliver the notifications. This should be enough
	 * to have the flag cleared only after we received the extra
	 * notification, if any.
	 */
	status = acpi_os_execute(OSL_NOTIFY_HANDLER,
			 rbtn_clear_suspended_flag, rbtn_data);
	if (ACPI_FAILURE(status))
		rbtn_clear_suspended_flag(rbtn_data);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(rbtn_pm_ops, rbtn_suspend, rbtn_resume);

static struct acpi_driver rbtn_driver = {
	.name = "dell-rbtn",
	.ids = rbtn_ids,
	.drv.pm = &rbtn_pm_ops,
	.ops = {
		.add = rbtn_add,
		.remove = rbtn_remove,
		.notify = rbtn_notify,
	},
	.owner = THIS_MODULE,
};


/*
 * notifier export functions
 */

static bool auto_remove_rfkill = true;

static ATOMIC_NOTIFIER_HEAD(rbtn_chain_head);

static int rbtn_inc_count(struct device *dev, void *data)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct rbtn_data *rbtn_data = device->driver_data;
	int *count = data;

	if (rbtn_data->type == RBTN_SLIDER)
		(*count)++;

	return 0;
}

static int rbtn_switch_dev(struct device *dev, void *data)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct rbtn_data *rbtn_data = device->driver_data;
	bool enable = data;

	if (rbtn_data->type != RBTN_SLIDER)
		return 0;

	if (enable)
		rbtn_rfkill_init(device);
	else
		rbtn_rfkill_exit(device);

	return 0;
}

int dell_rbtn_notifier_register(struct notifier_block *nb)
{
	bool first;
	int count;
	int ret;

	count = 0;
	ret = driver_for_each_device(&rbtn_driver.drv, NULL, &count,
				     rbtn_inc_count);
	if (ret || count == 0)
		return -ENODEV;

	first = !rbtn_chain_head.head;

	ret = atomic_notifier_chain_register(&rbtn_chain_head, nb);
	if (ret != 0)
		return ret;

	if (auto_remove_rfkill && first)
		ret = driver_for_each_device(&rbtn_driver.drv, NULL,
					     (void *)false, rbtn_switch_dev);

	return ret;
}
EXPORT_SYMBOL_GPL(dell_rbtn_notifier_register);

int dell_rbtn_notifier_unregister(struct notifier_block *nb)
{
	int ret;

	ret = atomic_notifier_chain_unregister(&rbtn_chain_head, nb);
	if (ret != 0)
		return ret;

	if (auto_remove_rfkill && !rbtn_chain_head.head)
		ret = driver_for_each_device(&rbtn_driver.drv, NULL,
					     (void *)true, rbtn_switch_dev);

	return ret;
}
EXPORT_SYMBOL_GPL(dell_rbtn_notifier_unregister);


/*
 * acpi driver functions
 */

static int rbtn_add(struct acpi_device *device)
{
	struct rbtn_data *rbtn_data;
	enum rbtn_type type;
	int ret = 0;

	type = rbtn_check(device);
	if (type == RBTN_UNKNOWN) {
		dev_info(&device->dev, "Unknown device type\n");
		return -EINVAL;
	}

	ret = rbtn_acquire(device, true);
	if (ret < 0) {
		dev_err(&device->dev, "Cannot enable device\n");
		return ret;
	}

	rbtn_data = devm_kzalloc(&device->dev, sizeof(*rbtn_data), GFP_KERNEL);
	if (!rbtn_data)
		return -ENOMEM;

	rbtn_data->type = type;
	device->driver_data = rbtn_data;

	switch (rbtn_data->type) {
	case RBTN_TOGGLE:
		ret = rbtn_input_init(rbtn_data);
		break;
	case RBTN_SLIDER:
		if (auto_remove_rfkill && rbtn_chain_head.head)
			ret = 0;
		else
			ret = rbtn_rfkill_init(device);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;

}

static int rbtn_remove(struct acpi_device *device)
{
	struct rbtn_data *rbtn_data = device->driver_data;

	switch (rbtn_data->type) {
	case RBTN_TOGGLE:
		rbtn_input_exit(rbtn_data);
		break;
	case RBTN_SLIDER:
		rbtn_rfkill_exit(device);
		break;
	default:
		break;
	}

	rbtn_acquire(device, false);
	device->driver_data = NULL;

	return 0;
}

static void rbtn_notify(struct acpi_device *device, u32 event)
{
	struct rbtn_data *rbtn_data = device->driver_data;

	/*
	 * Some BIOSes send a notification at resume.
	 * Ignore it to prevent unwanted input events.
	 */
	if (rbtn_data->suspended) {
		dev_dbg(&device->dev, "ACPI notification ignored\n");
		return;
	}

	if (event != 0x80) {
		dev_info(&device->dev, "Received unknown event (0x%x)\n",
			 event);
		return;
	}

	switch (rbtn_data->type) {
	case RBTN_TOGGLE:
		rbtn_input_event(rbtn_data);
		break;
	case RBTN_SLIDER:
		rbtn_rfkill_event(device);
		atomic_notifier_call_chain(&rbtn_chain_head, event, device);
		break;
	default:
		break;
	}
}


/*
 * module functions
 */

module_acpi_driver(rbtn_driver);

module_param(auto_remove_rfkill, bool, 0444);

MODULE_PARM_DESC(auto_remove_rfkill, "Automatically remove rfkill devices when "
				     "other modules start receiving events "
				     "from this module and re-add them when "
				     "the last module stops receiving events "
				     "(default true)");
MODULE_DEVICE_TABLE(acpi, rbtn_ids);
MODULE_DESCRIPTION("Dell Airplane Mode Switch driver");
MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_LICENSE("GPL");
