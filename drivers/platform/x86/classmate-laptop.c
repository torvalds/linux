// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2009  Thadeu Lima de Souza Cascardo <cascardo@holoscopio.com>
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/acpi.h>
#include <linux/backlight.h>
#include <linux/input.h>
#include <linux/rfkill.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>

struct cmpc_accel {
	int sensitivity;
	int g_select;
	int inputdev_state;
};

#define CMPC_ACCEL_DEV_STATE_CLOSED	0
#define CMPC_ACCEL_DEV_STATE_OPEN	1

#define CMPC_ACCEL_SENSITIVITY_DEFAULT		5
#define CMPC_ACCEL_G_SELECT_DEFAULT		0

#define CMPC_ACCEL_HID		"ACCE0000"
#define CMPC_ACCEL_HID_V4	"ACCE0001"
#define CMPC_TABLET_HID		"TBLT0000"
#define CMPC_IPML_HID	"IPML200"
#define CMPC_KEYS_HID		"FNBT0000"

/*
 * Generic input device code.
 */

typedef void (*input_device_init)(struct input_dev *dev);

static int cmpc_add_notify_device(struct device *dev, char *name,
				  input_device_init idev_init)
{
	struct input_dev *inputdev;
	int error;

	inputdev = input_allocate_device();
	if (!inputdev)
		return -ENOMEM;
	inputdev->name = name;
	inputdev->dev.parent = dev;
	idev_init(inputdev);
	error = input_register_device(inputdev);
	if (error) {
		input_free_device(inputdev);
		return error;
	}
	dev_set_drvdata(dev, inputdev);
	return 0;
}

static void cmpc_remove_notify_device(struct device *dev)
{
	input_unregister_device(dev_get_drvdata(dev));
}

/*
 * Accelerometer code for Classmate V4
 */
static acpi_status cmpc_start_accel_v4(acpi_handle handle)
{
	union acpi_object param[4];
	struct acpi_object_list input;
	acpi_status status;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0x3;
	param[1].type = ACPI_TYPE_INTEGER;
	param[1].integer.value = 0;
	param[2].type = ACPI_TYPE_INTEGER;
	param[2].integer.value = 0;
	param[3].type = ACPI_TYPE_INTEGER;
	param[3].integer.value = 0;
	input.count = 4;
	input.pointer = param;
	status = acpi_evaluate_object(handle, "ACMD", &input, NULL);
	return status;
}

static acpi_status cmpc_stop_accel_v4(acpi_handle handle)
{
	union acpi_object param[4];
	struct acpi_object_list input;
	acpi_status status;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0x4;
	param[1].type = ACPI_TYPE_INTEGER;
	param[1].integer.value = 0;
	param[2].type = ACPI_TYPE_INTEGER;
	param[2].integer.value = 0;
	param[3].type = ACPI_TYPE_INTEGER;
	param[3].integer.value = 0;
	input.count = 4;
	input.pointer = param;
	status = acpi_evaluate_object(handle, "ACMD", &input, NULL);
	return status;
}

static acpi_status cmpc_accel_set_sensitivity_v4(acpi_handle handle, int val)
{
	union acpi_object param[4];
	struct acpi_object_list input;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0x02;
	param[1].type = ACPI_TYPE_INTEGER;
	param[1].integer.value = val;
	param[2].type = ACPI_TYPE_INTEGER;
	param[2].integer.value = 0;
	param[3].type = ACPI_TYPE_INTEGER;
	param[3].integer.value = 0;
	input.count = 4;
	input.pointer = param;
	return acpi_evaluate_object(handle, "ACMD", &input, NULL);
}

static acpi_status cmpc_accel_set_g_select_v4(acpi_handle handle, int val)
{
	union acpi_object param[4];
	struct acpi_object_list input;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0x05;
	param[1].type = ACPI_TYPE_INTEGER;
	param[1].integer.value = val;
	param[2].type = ACPI_TYPE_INTEGER;
	param[2].integer.value = 0;
	param[3].type = ACPI_TYPE_INTEGER;
	param[3].integer.value = 0;
	input.count = 4;
	input.pointer = param;
	return acpi_evaluate_object(handle, "ACMD", &input, NULL);
}

static acpi_status cmpc_get_accel_v4(acpi_handle handle,
				     int16_t *x,
				     int16_t *y,
				     int16_t *z)
{
	union acpi_object param[4];
	struct acpi_object_list input;
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	int16_t *locs;
	acpi_status status;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0x01;
	param[1].type = ACPI_TYPE_INTEGER;
	param[1].integer.value = 0;
	param[2].type = ACPI_TYPE_INTEGER;
	param[2].integer.value = 0;
	param[3].type = ACPI_TYPE_INTEGER;
	param[3].integer.value = 0;
	input.count = 4;
	input.pointer = param;
	status = acpi_evaluate_object(handle, "ACMD", &input, &output);
	if (ACPI_SUCCESS(status)) {
		union acpi_object *obj;
		obj = output.pointer;
		locs = (int16_t *) obj->buffer.pointer;
		*x = locs[0];
		*y = locs[1];
		*z = locs[2];
		kfree(output.pointer);
	}
	return status;
}

static void cmpc_accel_handler_v4(acpi_handle handle, u32 event, void *data)
{
	struct device *dev = data;

	if (event == 0x81) {
		int16_t x, y, z;
		acpi_status status;

		status = cmpc_get_accel_v4(ACPI_HANDLE(dev), &x, &y, &z);
		if (ACPI_SUCCESS(status)) {
			struct input_dev *inputdev = dev_get_drvdata(dev);

			input_report_abs(inputdev, ABS_X, x);
			input_report_abs(inputdev, ABS_Y, y);
			input_report_abs(inputdev, ABS_Z, z);
			input_sync(inputdev);
		}
	}
}

static ssize_t cmpc_accel_sensitivity_show_v4(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct acpi_device *acpi;
	struct input_dev *inputdev;
	struct cmpc_accel *accel;

	acpi = to_acpi_device(dev);
	inputdev = dev_get_drvdata(&acpi->dev);
	if (!inputdev)
		return -ENXIO;

	accel = dev_get_drvdata(&inputdev->dev);
	if (!accel)
		return -ENXIO;

	return sysfs_emit(buf, "%d\n", accel->sensitivity);
}

static ssize_t cmpc_accel_sensitivity_store_v4(struct device *dev,
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
	if (!inputdev)
		return -ENXIO;

	accel = dev_get_drvdata(&inputdev->dev);
	if (!accel)
		return -ENXIO;

	r = kstrtoul(buf, 0, &sensitivity);
	if (r)
		return r;

	/* sensitivity must be between 1 and 127 */
	if (sensitivity < 1 || sensitivity > 127)
		return -EINVAL;

	accel->sensitivity = sensitivity;
	cmpc_accel_set_sensitivity_v4(acpi->handle, sensitivity);

	return strnlen(buf, count);
}

static struct device_attribute cmpc_accel_sensitivity_attr_v4 = {
	.attr = { .name = "sensitivity", .mode = 0660 },
	.show = cmpc_accel_sensitivity_show_v4,
	.store = cmpc_accel_sensitivity_store_v4
};

static ssize_t cmpc_accel_g_select_show_v4(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct acpi_device *acpi;
	struct input_dev *inputdev;
	struct cmpc_accel *accel;

	acpi = to_acpi_device(dev);
	inputdev = dev_get_drvdata(&acpi->dev);
	if (!inputdev)
		return -ENXIO;

	accel = dev_get_drvdata(&inputdev->dev);
	if (!accel)
		return -ENXIO;

	return sysfs_emit(buf, "%d\n", accel->g_select);
}

static ssize_t cmpc_accel_g_select_store_v4(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct acpi_device *acpi;
	struct input_dev *inputdev;
	struct cmpc_accel *accel;
	unsigned long g_select;
	int r;

	acpi = to_acpi_device(dev);
	inputdev = dev_get_drvdata(&acpi->dev);
	if (!inputdev)
		return -ENXIO;

	accel = dev_get_drvdata(&inputdev->dev);
	if (!accel)
		return -ENXIO;

	r = kstrtoul(buf, 0, &g_select);
	if (r)
		return r;

	/* 0 means 1.5g, 1 means 6g, everything else is wrong */
	if (g_select != 0 && g_select != 1)
		return -EINVAL;

	accel->g_select = g_select;
	cmpc_accel_set_g_select_v4(acpi->handle, g_select);

	return strnlen(buf, count);
}

static struct device_attribute cmpc_accel_g_select_attr_v4 = {
	.attr = { .name = "g_select", .mode = 0660 },
	.show = cmpc_accel_g_select_show_v4,
	.store = cmpc_accel_g_select_store_v4
};

static int cmpc_accel_open_v4(struct input_dev *input)
{
	acpi_handle handle = ACPI_HANDLE(input->dev.parent);
	struct cmpc_accel *accel;

	accel = dev_get_drvdata(&input->dev);
	if (!accel)
		return -ENXIO;

	cmpc_accel_set_sensitivity_v4(handle, accel->sensitivity);
	cmpc_accel_set_g_select_v4(handle, accel->g_select);

	if (ACPI_SUCCESS(cmpc_start_accel_v4(handle))) {
		accel->inputdev_state = CMPC_ACCEL_DEV_STATE_OPEN;
		return 0;
	}
	return -EIO;
}

static void cmpc_accel_close_v4(struct input_dev *input)
{
	struct cmpc_accel *accel;

	accel = dev_get_drvdata(&input->dev);

	cmpc_stop_accel_v4(ACPI_HANDLE(input->dev.parent));
	accel->inputdev_state = CMPC_ACCEL_DEV_STATE_CLOSED;
}

static void cmpc_accel_idev_init_v4(struct input_dev *inputdev)
{
	set_bit(EV_ABS, inputdev->evbit);
	input_set_abs_params(inputdev, ABS_X, -255, 255, 16, 0);
	input_set_abs_params(inputdev, ABS_Y, -255, 255, 16, 0);
	input_set_abs_params(inputdev, ABS_Z, -255, 255, 16, 0);
	inputdev->open = cmpc_accel_open_v4;
	inputdev->close = cmpc_accel_close_v4;
}

#ifdef CONFIG_PM_SLEEP
static int cmpc_accel_suspend_v4(struct device *dev)
{
	struct input_dev *inputdev;
	struct cmpc_accel *accel;

	inputdev = dev_get_drvdata(dev);
	accel = dev_get_drvdata(&inputdev->dev);

	if (accel->inputdev_state == CMPC_ACCEL_DEV_STATE_OPEN)
		return cmpc_stop_accel_v4(ACPI_HANDLE(dev));

	return 0;
}

static int cmpc_accel_resume_v4(struct device *dev)
{
	struct input_dev *inputdev;
	struct cmpc_accel *accel;

	inputdev = dev_get_drvdata(dev);
	accel = dev_get_drvdata(&inputdev->dev);

	if (accel->inputdev_state == CMPC_ACCEL_DEV_STATE_OPEN) {
		acpi_handle handle = ACPI_HANDLE(dev);

		cmpc_accel_set_sensitivity_v4(handle, accel->sensitivity);
		cmpc_accel_set_g_select_v4(handle, accel->g_select);

		if (ACPI_FAILURE(cmpc_start_accel_v4(handle)))
			return -EIO;
	}

	return 0;
}
#endif

static int cmpc_accel_probe_v4(struct platform_device *pdev)
{
	int error;
	struct input_dev *inputdev;
	struct cmpc_accel *accel;
	struct acpi_device *acpi;

	acpi = ACPI_COMPANION(&pdev->dev);
	if (!acpi)
		return -ENODEV;

	accel = devm_kzalloc(&pdev->dev, sizeof(*accel), GFP_KERNEL);
	if (!accel)
		return -ENOMEM;

	accel->inputdev_state = CMPC_ACCEL_DEV_STATE_CLOSED;

	error = cmpc_add_notify_device(&pdev->dev, "cmpc_accel_v4", cmpc_accel_idev_init_v4);
	if (error)
		return error;

	inputdev = dev_get_drvdata(&pdev->dev);
	dev_set_drvdata(&acpi->dev, inputdev);

	accel->sensitivity = CMPC_ACCEL_SENSITIVITY_DEFAULT;
	cmpc_accel_set_sensitivity_v4(acpi->handle, accel->sensitivity);

	error = device_create_file(&acpi->dev, &cmpc_accel_sensitivity_attr_v4);
	if (error)
		goto failed_sensitivity;

	accel->g_select = CMPC_ACCEL_G_SELECT_DEFAULT;
	cmpc_accel_set_g_select_v4(acpi->handle, accel->g_select);

	error = device_create_file(&acpi->dev, &cmpc_accel_g_select_attr_v4);
	if (error)
		goto failed_g_select;

	error = acpi_dev_install_notify_handler(acpi, ACPI_DEVICE_NOTIFY,
						cmpc_accel_handler_v4, &pdev->dev);
	if (error)
		goto failed_notify_handler;

	dev_set_drvdata(&inputdev->dev, accel);

	return 0;

failed_notify_handler:
	device_remove_file(&acpi->dev, &cmpc_accel_g_select_attr_v4);
failed_g_select:
	device_remove_file(&acpi->dev, &cmpc_accel_sensitivity_attr_v4);
failed_sensitivity:
	dev_set_drvdata(&acpi->dev, NULL);
	cmpc_remove_notify_device(&pdev->dev);
	return error;
}

static void cmpc_accel_remove_v4(struct platform_device *pdev)
{
	struct acpi_device *acpi = ACPI_COMPANION(&pdev->dev);

	acpi_dev_remove_notify_handler(acpi, ACPI_DEVICE_NOTIFY,
				       cmpc_accel_handler_v4);
	device_remove_file(&acpi->dev, &cmpc_accel_g_select_attr_v4);
	device_remove_file(&acpi->dev, &cmpc_accel_sensitivity_attr_v4);
	dev_set_drvdata(&acpi->dev, NULL);
	cmpc_remove_notify_device(&pdev->dev);
}

static SIMPLE_DEV_PM_OPS(cmpc_accel_pm, cmpc_accel_suspend_v4,
			 cmpc_accel_resume_v4);

static const struct acpi_device_id cmpc_accel_device_ids_v4[] = {
	{CMPC_ACCEL_HID_V4, 0},
	{"", 0}
};

static struct platform_driver cmpc_accel_acpi_driver_v4 = {
	.probe = cmpc_accel_probe_v4,
	.remove = cmpc_accel_remove_v4,
	.driver = {
		.name = "cmpc_accel_v4",
		.acpi_match_table = cmpc_accel_device_ids_v4,
		.pm = &cmpc_accel_pm,
	},
};


/*
 * Accelerometer code for Classmate versions prior to V4
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
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
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

static void cmpc_accel_handler(acpi_handle handle, u32 event, void *data)
{
	struct device *dev = data;

	if (event == 0x81) {
		unsigned char x, y, z;
		acpi_status status;

		status = cmpc_get_accel(ACPI_HANDLE(dev), &x, &y, &z);
		if (ACPI_SUCCESS(status)) {
			struct input_dev *inputdev = dev_get_drvdata(dev);

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
	if (!inputdev)
		return -ENXIO;

	accel = dev_get_drvdata(&inputdev->dev);
	if (!accel)
		return -ENXIO;

	return sysfs_emit(buf, "%d\n", accel->sensitivity);
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
	if (!inputdev)
		return -ENXIO;

	accel = dev_get_drvdata(&inputdev->dev);
	if (!accel)
		return -ENXIO;

	r = kstrtoul(buf, 0, &sensitivity);
	if (r)
		return r;

	accel->sensitivity = sensitivity;
	cmpc_accel_set_sensitivity(acpi->handle, sensitivity);

	return strnlen(buf, count);
}

static struct device_attribute cmpc_accel_sensitivity_attr = {
	.attr = { .name = "sensitivity", .mode = 0660 },
	.show = cmpc_accel_sensitivity_show,
	.store = cmpc_accel_sensitivity_store
};

static int cmpc_accel_open(struct input_dev *input)
{
	if (ACPI_SUCCESS(cmpc_start_accel(ACPI_HANDLE(input->dev.parent))))
		return 0;
	return -EIO;
}

static void cmpc_accel_close(struct input_dev *input)
{
	cmpc_stop_accel(ACPI_HANDLE(input->dev.parent));
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

static int cmpc_accel_probe(struct platform_device *pdev)
{
	int error;
	struct input_dev *inputdev;
	struct cmpc_accel *accel;
	struct acpi_device *acpi;

	acpi = ACPI_COMPANION(&pdev->dev);
	if (!acpi)
		return -ENODEV;

	accel = devm_kzalloc(&pdev->dev, sizeof(*accel), GFP_KERNEL);
	if (!accel)
		return -ENOMEM;

	error = cmpc_add_notify_device(&pdev->dev, "cmpc_accel", cmpc_accel_idev_init);
	if (error)
		return error;

	inputdev = dev_get_drvdata(&pdev->dev);
	dev_set_drvdata(&acpi->dev, inputdev);

	accel->sensitivity = CMPC_ACCEL_SENSITIVITY_DEFAULT;
	cmpc_accel_set_sensitivity(acpi->handle, accel->sensitivity);

	error = device_create_file(&acpi->dev, &cmpc_accel_sensitivity_attr);
	if (error)
		goto failed_file;

	error = acpi_dev_install_notify_handler(acpi, ACPI_DEVICE_NOTIFY,
						cmpc_accel_handler, &pdev->dev);
	if (error)
		goto failed_notify_handler;

	dev_set_drvdata(&inputdev->dev, accel);

	return 0;

failed_notify_handler:
	device_remove_file(&acpi->dev, &cmpc_accel_sensitivity_attr);
failed_file:
	dev_set_drvdata(&acpi->dev, NULL);
	cmpc_remove_notify_device(&pdev->dev);
	return error;
}

static void cmpc_accel_remove(struct platform_device *pdev)
{
	struct acpi_device *acpi = ACPI_COMPANION(&pdev->dev);

	acpi_dev_remove_notify_handler(acpi, ACPI_DEVICE_NOTIFY,
				       cmpc_accel_handler);
	device_remove_file(&acpi->dev, &cmpc_accel_sensitivity_attr);
	dev_set_drvdata(&acpi->dev, NULL);
	cmpc_remove_notify_device(&pdev->dev);
}

static const struct acpi_device_id cmpc_accel_device_ids[] = {
	{CMPC_ACCEL_HID, 0},
	{"", 0}
};

static struct platform_driver cmpc_accel_acpi_driver = {
	.probe = cmpc_accel_probe,
	.remove = cmpc_accel_remove,
	.driver = {
		.name = "cmpc_accel",
		.acpi_match_table = cmpc_accel_device_ids,
	},
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

static void cmpc_tablet_handler(acpi_handle handle, u32 event, void *data)
{
	struct device *dev = data;
	unsigned long long val = 0;
	struct input_dev *inputdev = dev_get_drvdata(dev);

	if (event == 0x81) {
		if (ACPI_SUCCESS(cmpc_get_tablet(ACPI_HANDLE(dev), &val))) {
			input_report_switch(inputdev, SW_TABLET_MODE, !val);
			input_sync(inputdev);
		}
	}
}

static void cmpc_tablet_idev_init(struct input_dev *inputdev)
{
	acpi_handle handle = ACPI_HANDLE(inputdev->dev.parent);
	unsigned long long val = 0;

	set_bit(EV_SW, inputdev->evbit);
	set_bit(SW_TABLET_MODE, inputdev->swbit);

	if (ACPI_SUCCESS(cmpc_get_tablet(handle, &val))) {
		input_report_switch(inputdev, SW_TABLET_MODE, !val);
		input_sync(inputdev);
	}
}

static int cmpc_tablet_probe(struct platform_device *pdev)
{
	struct acpi_device *acpi;
	int error;

	acpi = ACPI_COMPANION(&pdev->dev);
	if (!acpi)
		return -ENODEV;

	error = cmpc_add_notify_device(&pdev->dev, "cmpc_tablet", cmpc_tablet_idev_init);
	if (error)
		return error;

	error = acpi_dev_install_notify_handler(acpi, ACPI_DEVICE_NOTIFY,
						cmpc_tablet_handler, &pdev->dev);
	if (error)
		cmpc_remove_notify_device(&pdev->dev);

	return error;
}

static void cmpc_tablet_remove(struct platform_device *pdev)
{
	acpi_dev_remove_notify_handler(ACPI_COMPANION(&pdev->dev),
				       ACPI_DEVICE_NOTIFY, cmpc_tablet_handler);
	cmpc_remove_notify_device(&pdev->dev);
}

#ifdef CONFIG_PM_SLEEP
static int cmpc_tablet_resume(struct device *dev)
{
	struct input_dev *inputdev = dev_get_drvdata(dev);

	unsigned long long val = 0;
	if (ACPI_SUCCESS(cmpc_get_tablet(ACPI_HANDLE(dev), &val))) {
		input_report_switch(inputdev, SW_TABLET_MODE, !val);
		input_sync(inputdev);
	}
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cmpc_tablet_pm, NULL, cmpc_tablet_resume);

static const struct acpi_device_id cmpc_tablet_device_ids[] = {
	{CMPC_TABLET_HID, 0},
	{"", 0}
};

static struct platform_driver cmpc_tablet_acpi_driver = {
	.probe = cmpc_tablet_probe,
	.remove = cmpc_tablet_remove,
	.driver = {
		.name = "cmpc_tablet",
		.acpi_match_table = cmpc_tablet_device_ids,
		.pm = &cmpc_tablet_pm,
	},
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

static const struct backlight_ops cmpc_bl_ops = {
	.get_brightness = cmpc_bl_get_brightness,
	.update_status = cmpc_bl_update_status
};

/*
 * RFKILL code.
 */

static acpi_status cmpc_get_rfkill_wlan(acpi_handle handle,
					unsigned long long *value)
{
	union acpi_object param;
	struct acpi_object_list input;
	unsigned long long output;
	acpi_status status;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = 0xC1;
	input.count = 1;
	input.pointer = &param;
	status = acpi_evaluate_integer(handle, "GRDI", &input, &output);
	if (ACPI_SUCCESS(status))
		*value = output;
	return status;
}

static acpi_status cmpc_set_rfkill_wlan(acpi_handle handle,
					unsigned long long value)
{
	union acpi_object param[2];
	struct acpi_object_list input;
	acpi_status status;
	unsigned long long output;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = 0xC1;
	param[1].type = ACPI_TYPE_INTEGER;
	param[1].integer.value = value;
	input.count = 2;
	input.pointer = param;
	status = acpi_evaluate_integer(handle, "GWRI", &input, &output);
	return status;
}

static void cmpc_rfkill_query(struct rfkill *rfkill, void *data)
{
	acpi_status status;
	acpi_handle handle;
	unsigned long long state;
	bool blocked;

	handle = data;
	status = cmpc_get_rfkill_wlan(handle, &state);
	if (ACPI_SUCCESS(status)) {
		blocked = state & 1 ? false : true;
		rfkill_set_sw_state(rfkill, blocked);
	}
}

static int cmpc_rfkill_block(void *data, bool blocked)
{
	acpi_status status;
	acpi_handle handle;
	unsigned long long state;
	bool is_blocked;

	handle = data;
	status = cmpc_get_rfkill_wlan(handle, &state);
	if (ACPI_FAILURE(status))
		return -ENODEV;
	/* Check if we really need to call cmpc_set_rfkill_wlan */
	is_blocked = state & 1 ? false : true;
	if (is_blocked != blocked) {
		state = blocked ? 0 : 1;
		status = cmpc_set_rfkill_wlan(handle, state);
		if (ACPI_FAILURE(status))
			return -ENODEV;
	}
	return 0;
}

static const struct rfkill_ops cmpc_rfkill_ops = {
	.query = cmpc_rfkill_query,
	.set_block = cmpc_rfkill_block,
};

/*
 * Common backlight and rfkill code.
 */

struct ipml200_dev {
	struct backlight_device *bd;
	struct rfkill *rf;
};

static int cmpc_ipml_probe(struct platform_device *pdev)
{
	int retval;
	struct ipml200_dev *ipml;
	struct backlight_properties props;
	acpi_handle handle;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return -ENODEV;

	ipml = kmalloc_obj(*ipml);
	if (ipml == NULL)
		return -ENOMEM;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = 7;
	ipml->bd = backlight_device_register("cmpc_bl", &pdev->dev,
					     handle, &cmpc_bl_ops,
					     &props);
	if (IS_ERR(ipml->bd)) {
		retval = PTR_ERR(ipml->bd);
		goto out_bd;
	}

	ipml->rf = rfkill_alloc("cmpc_rfkill", &pdev->dev, RFKILL_TYPE_WLAN,
				&cmpc_rfkill_ops, handle);
	/*
	 * If RFKILL is disabled, rfkill_alloc will return ERR_PTR(-ENODEV).
	 * This is OK, however, since all other uses of the device will not
	 * dereference it.
	 */
	if (ipml->rf) {
		retval = rfkill_register(ipml->rf);
		if (retval) {
			rfkill_destroy(ipml->rf);
			ipml->rf = NULL;
		}
	}

	platform_set_drvdata(pdev, ipml);
	return 0;

out_bd:
	kfree(ipml);
	return retval;
}

static void cmpc_ipml_remove(struct platform_device *pdev)
{
	struct ipml200_dev *ipml;

	ipml = platform_get_drvdata(pdev);

	backlight_device_unregister(ipml->bd);

	if (ipml->rf) {
		rfkill_unregister(ipml->rf);
		rfkill_destroy(ipml->rf);
	}

	kfree(ipml);
}

static const struct acpi_device_id cmpc_ipml_device_ids[] = {
	{CMPC_IPML_HID, 0},
	{"", 0}
};

static struct platform_driver cmpc_ipml_acpi_driver = {
	.probe = cmpc_ipml_probe,
	.remove = cmpc_ipml_remove,
	.driver = {
		.name = "cmpc",
		.acpi_match_table = cmpc_ipml_device_ids,
	},
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
	KEY_UNKNOWN,
	KEY_CAMERA,
	KEY_BACK,
	KEY_FORWARD,
	KEY_UNKNOWN,
	KEY_WLAN, /* NL3: 0x8b (press), 0x9b (release) */
	KEY_MAX
};

static void cmpc_keys_handler(acpi_handle handle, u32 event, void *data)
{
	struct device *dev = data;
	struct input_dev *inputdev;
	int code = KEY_MAX;

	if ((event & 0x0F) < ARRAY_SIZE(cmpc_keys_codes))
		code = cmpc_keys_codes[event & 0x0F];
	inputdev = dev_get_drvdata(dev);
	input_report_key(inputdev, code, !(event & 0x10));
	input_sync(inputdev);
}

static void cmpc_keys_idev_init(struct input_dev *inputdev)
{
	int i;

	set_bit(EV_KEY, inputdev->evbit);
	for (i = 0; cmpc_keys_codes[i] != KEY_MAX; i++)
		set_bit(cmpc_keys_codes[i], inputdev->keybit);
}

static int cmpc_keys_probe(struct platform_device *pdev)
{
	struct acpi_device *acpi;
	int error;

	acpi = ACPI_COMPANION(&pdev->dev);
	if (!acpi)
		return -ENODEV;

	error = cmpc_add_notify_device(&pdev->dev, "cmpc_keys", cmpc_keys_idev_init);
	if (error)
		return error;

	error = acpi_dev_install_notify_handler(acpi, ACPI_DEVICE_NOTIFY,
						cmpc_keys_handler, &pdev->dev);
	if (error)
		cmpc_remove_notify_device(&pdev->dev);

	return error;
}

static void cmpc_keys_remove(struct platform_device *pdev)
{
	acpi_dev_remove_notify_handler(ACPI_COMPANION(&pdev->dev),
				       ACPI_DEVICE_NOTIFY, cmpc_keys_handler);
	cmpc_remove_notify_device(&pdev->dev);
}

static const struct acpi_device_id cmpc_keys_device_ids[] = {
	{CMPC_KEYS_HID, 0},
	{"", 0}
};

static struct platform_driver cmpc_keys_acpi_driver = {
	.probe = cmpc_keys_probe,
	.remove = cmpc_keys_remove,
	.driver = {
		.name = "cmpc_keys",
		.acpi_match_table = cmpc_keys_device_ids,
	},
};


/*
 * General init/exit code.
 */

static int cmpc_init(void)
{
	int r;

	r = platform_driver_register(&cmpc_keys_acpi_driver);
	if (r)
		goto failed_keys;

	r = platform_driver_register(&cmpc_ipml_acpi_driver);
	if (r)
		goto failed_bl;

	r = platform_driver_register(&cmpc_tablet_acpi_driver);
	if (r)
		goto failed_tablet;

	r = platform_driver_register(&cmpc_accel_acpi_driver);
	if (r)
		goto failed_accel;

	r = platform_driver_register(&cmpc_accel_acpi_driver_v4);
	if (r)
		goto failed_accel_v4;

	return r;

failed_accel_v4:
	platform_driver_unregister(&cmpc_accel_acpi_driver);

failed_accel:
	platform_driver_unregister(&cmpc_tablet_acpi_driver);

failed_tablet:
	platform_driver_unregister(&cmpc_ipml_acpi_driver);

failed_bl:
	platform_driver_unregister(&cmpc_keys_acpi_driver);

failed_keys:
	return r;
}

static void cmpc_exit(void)
{
	platform_driver_unregister(&cmpc_accel_acpi_driver_v4);
	platform_driver_unregister(&cmpc_accel_acpi_driver);
	platform_driver_unregister(&cmpc_tablet_acpi_driver);
	platform_driver_unregister(&cmpc_ipml_acpi_driver);
	platform_driver_unregister(&cmpc_keys_acpi_driver);
}

module_init(cmpc_init);
module_exit(cmpc_exit);

static const struct acpi_device_id cmpc_device_ids[] __maybe_unused = {
	{CMPC_ACCEL_HID, 0},
	{CMPC_ACCEL_HID_V4, 0},
	{CMPC_TABLET_HID, 0},
	{CMPC_IPML_HID, 0},
	{CMPC_KEYS_HID, 0},
	{"", 0}
};

MODULE_DEVICE_TABLE(acpi, cmpc_device_ids);
MODULE_DESCRIPTION("Support for Intel Classmate PC ACPI devices");
MODULE_LICENSE("GPL");
