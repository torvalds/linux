/*
 *  Copyright (C) 2009  Thadeu Lima de Souza Cascardo <cascardo@holoscopio.com>
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <acpi/acpi_drivers.h>
#include <linux/backlight.h>
#include <linux/input.h>

MODULE_LICENSE("GPL");


struct cmpc_accel {
	int sensitivity;
};

#define CMPC_ACCEL_SENSITIVITY_DEFAULT		5


#define CMPC_ACCEL_HID		"ACCE0000"
#define CMPC_TABLET_HID		"TBLT0000"
#define CMPC_BL_HID		"IPML200"
#define CMPC_KEYS_HID		"FnBT0000"

/*
 * Generic input device code.
 */

typedef void (*input_device_init)(struct input_dev *dev);

static int cmpc_add_acpi_notify_device(struct acpi_device *acpi, char *name,
				       input_device_init idev_init)
{
	struct input_dev *inputdev;
	int error;

	inputdev = input_allocate_device();
	if (!inputdev)
		return -ENOMEM;
	inputdev->name = name;
	inputdev->dev.parent = &acpi->dev;
	idev_init(inputdev);
	error = input_register_device(inputdev);
	if (error) {
		input_free_device(inputdev);
		return error;
	}
	dev_set_drvdata(&acpi->dev, inputdev);
	return 0;
}

static int cmpc_remove_acpi_notify_device(struct acpi_device *acpi)
{
	struct input_dev *inputdev = dev_get_drvdata(&acpi->dev);
	input_unregister_device(inputdev);
	return 0;
}

/*
 * Accelerometer code.
 */
static acpi_status cmpc_start_accel(acpi_handle handle)
{
	union acpi_object param[2];
	struct acpi_object_list input;
	acpi_status status;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0x3;
	param[1].type = ACPI_TYPE_INTEGER;
	input.count = 2;
	input.pointer = param;
	status = acpi_evaluate_object(handle, "ACMD", &input, NULL);
	return status;
}

static acpi_status cmpc_stop_accel(acpi_handle handle)
{
	union acpi_object param[2];
	struct acpi_object_list input;
	acpi_status status;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0x4;
	param[1].type = ACPI_TYPE_INTEGER;
	input.count = 2;
	input.pointer = param;
	status = acpi_evaluate_object(handle, "ACMD", &input, NULL);
	return status;
}

static acpi_status cmpc_accel_set_sensitivity(acpi_handle handle, int val)
{
	union acpi_object param[2];
	struct acpi_object_list input;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0x02;
	param[1].type = ACPI_TYPE_INTEGER;
	param[1].integer.value = val;
	input.count = 2;
	input.pointer = param;
	return acpi_evaluate_object(handle, "ACMD", &input, NULL);
}

static acpi_status cmpc_get_accel(acpi_handle handle,
				  unsigned char *x,
				  unsigned char *y,
				  unsigned char *z)
{
	union acpi_object param[2];
	struct acpi_object_list input;
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, 0 };
	unsigned char *locs;
	acpi_status status;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0x01;
	param[1].type = ACPI_TYPE_INTEGER;
	input.count = 2;
	input.pointer = param;
	status = acpi_evaluate_object(handle, "ACMD", &input, &output);
	if (ACPI_SUCCESS(status)) {
		union acpi_object *obj;
		obj = output.pointer;
		locs = obj->buffer.pointer;
		*x = locs[0];
		*y = locs[1];
		*z = locs[2];
		kfree(output.pointer);
	}
	return status;
}

static void cmpc_accel_handler(struct acpi_device *dev, u32 event)
{
	if (event == 0x81) {
		unsigned char x, y, z;
		acpi_status status;

		status = cmpc_get_accel(dev->handle, &x, &y, &z);
		if (ACPI_SUCCESS(status)) {
			struct input_dev *inputdev = dev_get_drvdata(&dev->dev);

			input_report_abs(inputdev, ABS_X, x);
			input_report_abs(inputdev, ABS_Y, y);
			input_report_abs(inputdev, ABS_Z, z);
			input_sync(inputdev);
		}
	}
}

static ssize_t cmpc_accel_sensitivity_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct acpi_device *acpi;
	struct input_dev *inputdev;
	struct cmpc_accel *accel;

	acpi = to_acpi_device(dev);
	inputdev = dev_get_drvdata(&acpi->dev);
	accel = dev_get_drvdata(&inputdev->dev);

	return sprintf(buf, "%d\n", accel->sensitivity);
}

static ssize_t cmpc_accel_sensitivity_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct acpi_device *acpi;
	struct input_dev *inputdev;
	struct cmpc_accel *accel;
	unsigned long sensitivity;
	int r;

	acpi = to_acpi_device(dev);
	inputdev = dev_get_drvdata(&acpi->dev);
	accel = dev_get_drvdata(&inputdev->dev);

	r = strict_strtoul(buf, 0, &sensitivity);
	if (r)
		return r;

	accel->sensitivity = sensitivity;
	cmpc_accel_set_sensitivity(acpi->handle, sensitivity);

	return strnlen(buf, count);
}

struct device_attribute cmpc_accel_sensitivity_attr = {
	.attr = { .name = "sensitivity", .mode = 0660 },
	.show = cmpc_accel_sensitivity_show,
	.store = cmpc_accel_sensitivity_store
};

static int cmpc_accel_open(struct input_dev *input)
{
	struct acpi_device *acpi;

	acpi = to_acpi_device(input->dev.parent);
	if (ACPI_SUCCESS(cmpc_start_accel(acpi->handle)))
		return 0;
	return -EIO;
}

static void cmpc_accel_close(struct input_dev *input)
{
	struct acpi_device *acpi;

	acpi = to_acpi_device(input->dev.parent);
	cmpc_stop_accel(acpi->handle);
}

static void cmpc_accel_idev_init(struct input_dev *inputdev)
{
	set_bit(EV_ABS, inputdev->evbit);
	input_set_abs_params(inputdev, ABS_X, 0, 255, 8, 0);
	input_set_abs_params(inputdev, ABS_Y, 0, 255, 8, 0);
	input_set_abs_params(inputdev, ABS_Z, 0, 255, 8, 0);
	inputdev->open = cmpc_accel_open;
	inputdev->close = cmpc_accel_close;
}

static int cmpc_accel_add(struct acpi_device *acpi)
{
	int error;
	struct input_dev *inputdev;
	struct cmpc_accel *accel;

	accel = kmalloc(sizeof(*accel), GFP_KERNEL);
	if (!accel)
		return -ENOMEM;

	accel->sensitivity = CMPC_ACCEL_SENSITIVITY_DEFAULT;
	cmpc_accel_set_sensitivity(acpi->handle, accel->sensitivity);

	error = device_create_file(&acpi->dev, &cmpc_accel_sensitivity_attr);
	if (error)
		goto failed_file;

	error = cmpc_add_acpi_notify_device(acpi, "cmpc_accel",
					    cmpc_accel_idev_init);
	if (error)
		goto failed_input;

	inputdev = dev_get_drvdata(&acpi->dev);
	dev_set_drvdata(&inputdev->dev, accel);

	return 0;

failed_input:
	device_remove_file(&acpi->dev, &cmpc_accel_sensitivity_attr);
failed_file:
	kfree(accel);
	return error;
}

static int cmpc_accel_remove(struct acpi_device *acpi, int type)
{
	struct input_dev *inputdev;
	struct cmpc_accel *accel;

	inputdev = dev_get_drvdata(&acpi->dev);
	accel = dev_get_drvdata(&inputdev->dev);

	device_remove_file(&acpi->dev, &cmpc_accel_sensitivity_attr);
	return cmpc_remove_acpi_notify_device(acpi);
}

static const struct acpi_device_id cmpc_accel_device_ids[] = {
	{CMPC_ACCEL_HID, 0},
	{"", 0}
};

static struct acpi_driver cmpc_accel_acpi_driver = {
	.owner = THIS_MODULE,
	.name = "cmpc_accel",
	.class = "cmpc_accel",
	.ids = cmpc_accel_device_ids,
	.ops = {
		.add = cmpc_accel_add,
		.remove = cmpc_accel_remove,
		.notify = cmpc_accel_handler,
	}
};


/*
 * Tablet mode code.
 */
static acpi_status cmpc_get_tablet(acpi_handle handle,
				   unsigned long long *value)
{
	union acpi_object param;
	struct acpi_object_list input;
	unsigned long long output;
	acpi_status status;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = 0x01;
	input.count = 1;
	input.pointer = &param;
	status = acpi_evaluate_integer(handle, "TCMD", &input, &output);
	if (ACPI_SUCCESS(status))
		*value = output;
	return status;
}

static void cmpc_tablet_handler(struct acpi_device *dev, u32 event)
{
	unsigned long long val = 0;
	struct input_dev *inputdev = dev_get_drvdata(&dev->dev);

	if (event == 0x81) {
		if (ACPI_SUCCESS(cmpc_get_tablet(dev->handle, &val)))
			input_report_switch(inputdev, SW_TABLET_MODE, !val);
	}
}

static void cmpc_tablet_idev_init(struct input_dev *inputdev)
{
	unsigned long long val = 0;
	struct acpi_device *acpi;

	set_bit(EV_SW, inputdev->evbit);
	set_bit(SW_TABLET_MODE, inputdev->swbit);

	acpi = to_acpi_device(inputdev->dev.parent);
	if (ACPI_SUCCESS(cmpc_get_tablet(acpi->handle, &val)))
		input_report_switch(inputdev, SW_TABLET_MODE, !val);
}

static int cmpc_tablet_add(struct acpi_device *acpi)
{
	return cmpc_add_acpi_notify_device(acpi, "cmpc_tablet",
					   cmpc_tablet_idev_init);
}

static int cmpc_tablet_remove(struct acpi_device *acpi, int type)
{
	return cmpc_remove_acpi_notify_device(acpi);
}

static int cmpc_tablet_resume(struct acpi_device *acpi)
{
	struct input_dev *inputdev = dev_get_drvdata(&acpi->dev);
	unsigned long long val = 0;
	if (ACPI_SUCCESS(cmpc_get_tablet(acpi->handle, &val)))
		input_report_switch(inputdev, SW_TABLET_MODE, !val);
	return 0;
}

static const struct acpi_device_id cmpc_tablet_device_ids[] = {
	{CMPC_TABLET_HID, 0},
	{"", 0}
};

static struct acpi_driver cmpc_tablet_acpi_driver = {
	.owner = THIS_MODULE,
	.name = "cmpc_tablet",
	.class = "cmpc_tablet",
	.ids = cmpc_tablet_device_ids,
	.ops = {
		.add = cmpc_tablet_add,
		.remove = cmpc_tablet_remove,
		.resume = cmpc_tablet_resume,
		.notify = cmpc_tablet_handler,
	}
};


/*
 * Backlight code.
 */

static acpi_status cmpc_get_brightness(acpi_handle handle,
				       unsigned long long *value)
{
	union acpi_object param;
	struct acpi_object_list input;
	unsigned long long output;
	acpi_status status;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = 0xC0;
	input.count = 1;
	input.pointer = &param;
	status = acpi_evaluate_integer(handle, "GRDI", &input, &output);
	if (ACPI_SUCCESS(status))
		*value = output;
	return status;
}

static acpi_status cmpc_set_brightness(acpi_handle handle,
				       unsigned long long value)
{
	union acpi_object param[2];
	struct acpi_object_list input;
	acpi_status status;
	unsigned long long output;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0xC0;
	param[1].type = ACPI_TYPE_INTEGER;
	param[1].integer.value = value;
	input.count = 2;
	input.pointer = param;
	status = acpi_evaluate_integer(handle, "GWRI", &input, &output);
	return status;
}

static int cmpc_bl_get_brightness(struct backlight_device *bd)
{
	acpi_status status;
	acpi_handle handle;
	unsigned long long brightness;

	handle = bl_get_data(bd);
	status = cmpc_get_brightness(handle, &brightness);
	if (ACPI_SUCCESS(status))
		return brightness;
	else
		return -1;
}

static int cmpc_bl_update_status(struct backlight_device *bd)
{
	acpi_status status;
	acpi_handle handle;

	handle = bl_get_data(bd);
	status = cmpc_set_brightness(handle, bd->props.brightness);
	if (ACPI_SUCCESS(status))
		return 0;
	else
		return -1;
}

static struct backlight_ops cmpc_bl_ops = {
	.get_brightness = cmpc_bl_get_brightness,
	.update_status = cmpc_bl_update_status
};

static int cmpc_bl_add(struct acpi_device *acpi)
{
	struct backlight_device *bd;

	bd = backlight_device_register("cmpc_bl", &acpi->dev,
				       acpi->handle, &cmpc_bl_ops);
	bd->props.max_brightness = 7;
	dev_set_drvdata(&acpi->dev, bd);
	return 0;
}

static int cmpc_bl_remove(struct acpi_device *acpi, int type)
{
	struct backlight_device *bd;

	bd = dev_get_drvdata(&acpi->dev);
	backlight_device_unregister(bd);
	return 0;
}

static const struct acpi_device_id cmpc_bl_device_ids[] = {
	{CMPC_BL_HID, 0},
	{"", 0}
};

static struct acpi_driver cmpc_bl_acpi_driver = {
	.owner = THIS_MODULE,
	.name = "cmpc",
	.class = "cmpc",
	.ids = cmpc_bl_device_ids,
	.ops = {
		.add = cmpc_bl_add,
		.remove = cmpc_bl_remove
	}
};


/*
 * Extra keys code.
 */
static int cmpc_keys_codes[] = {
	KEY_UNKNOWN,
	KEY_WLAN,
	KEY_SWITCHVIDEOMODE,
	KEY_BRIGHTNESSDOWN,
	KEY_BRIGHTNESSUP,
	KEY_VENDOR,
	KEY_MAX
};

static void cmpc_keys_handler(struct acpi_device *dev, u32 event)
{
	struct input_dev *inputdev;
	int code = KEY_MAX;

	if ((event & 0x0F) < ARRAY_SIZE(cmpc_keys_codes))
		code = cmpc_keys_codes[event & 0x0F];
	inputdev = dev_get_drvdata(&dev->dev);;
	input_report_key(inputdev, code, !(event & 0x10));
}

static void cmpc_keys_idev_init(struct input_dev *inputdev)
{
	int i;

	set_bit(EV_KEY, inputdev->evbit);
	for (i = 0; cmpc_keys_codes[i] != KEY_MAX; i++)
		set_bit(cmpc_keys_codes[i], inputdev->keybit);
}

static int cmpc_keys_add(struct acpi_device *acpi)
{
	return cmpc_add_acpi_notify_device(acpi, "cmpc_keys",
					   cmpc_keys_idev_init);
}

static int cmpc_keys_remove(struct acpi_device *acpi, int type)
{
	return cmpc_remove_acpi_notify_device(acpi);
}

static const struct acpi_device_id cmpc_keys_device_ids[] = {
	{CMPC_KEYS_HID, 0},
	{"", 0}
};

static struct acpi_driver cmpc_keys_acpi_driver = {
	.owner = THIS_MODULE,
	.name = "cmpc_keys",
	.class = "cmpc_keys",
	.ids = cmpc_keys_device_ids,
	.ops = {
		.add = cmpc_keys_add,
		.remove = cmpc_keys_remove,
		.notify = cmpc_keys_handler,
	}
};


/*
 * General init/exit code.
 */

static int cmpc_init(void)
{
	int r;

	r = acpi_bus_register_driver(&cmpc_keys_acpi_driver);
	if (r)
		goto failed_keys;

	r = acpi_bus_register_driver(&cmpc_bl_acpi_driver);
	if (r)
		goto failed_bl;

	r = acpi_bus_register_driver(&cmpc_tablet_acpi_driver);
	if (r)
		goto failed_tablet;

	r = acpi_bus_register_driver(&cmpc_accel_acpi_driver);
	if (r)
		goto failed_accel;

	return r;

failed_accel:
	acpi_bus_unregister_driver(&cmpc_tablet_acpi_driver);

failed_tablet:
	acpi_bus_unregister_driver(&cmpc_bl_acpi_driver);

failed_bl:
	acpi_bus_unregister_driver(&cmpc_keys_acpi_driver);

failed_keys:
	return r;
}

static void cmpc_exit(void)
{
	acpi_bus_unregister_driver(&cmpc_accel_acpi_driver);
	acpi_bus_unregister_driver(&cmpc_tablet_acpi_driver);
	acpi_bus_unregister_driver(&cmpc_bl_acpi_driver);
	acpi_bus_unregister_driver(&cmpc_keys_acpi_driver);
}

module_init(cmpc_init);
module_exit(cmpc_exit);

static const struct acpi_device_id cmpc_device_ids[] = {
	{CMPC_ACCEL_HID, 0},
	{CMPC_TABLET_HID, 0},
	{CMPC_BL_HID, 0},
	{CMPC_KEYS_HID, 0},
	{"", 0}
};

MODULE_DEVICE_TABLE(acpi, cmpc_device_ids);
