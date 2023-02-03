// SPDX-License-Identifier: GPL-2.0
/*
 *  Generic Loongson processor based LAPTOP/ALL-IN-ONE driver
 *
 *  Jianmin Lv <lvjianmin@loongson.cn>
 *  Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/backlight.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>
#include <acpi/video.h>

/* 1. Driver-wide structs and misc. variables */

/* ACPI HIDs */
#define LOONGSON_ACPI_EC_HID	"PNP0C09"
#define LOONGSON_ACPI_HKEY_HID	"LOON0000"

#define ACPI_LAPTOP_NAME "loongson-laptop"
#define ACPI_LAPTOP_ACPI_EVENT_PREFIX "loongson"

#define MAX_ACPI_ARGS			3
#define GENERIC_HOTKEY_MAP_MAX		64

#define GENERIC_EVENT_TYPE_OFF		12
#define GENERIC_EVENT_TYPE_MASK		0xF000
#define GENERIC_EVENT_CODE_MASK		0x0FFF

struct generic_sub_driver {
	u32 type;
	char *name;
	acpi_handle *handle;
	struct acpi_device *device;
	struct platform_driver *driver;
	int (*init)(struct generic_sub_driver *sub_driver);
	void (*notify)(struct generic_sub_driver *sub_driver, u32 event);
	u8 acpi_notify_installed;
};

static u32 input_device_registered;
static struct input_dev *generic_inputdev;

static acpi_handle hotkey_handle;
static struct key_entry hotkey_keycode_map[GENERIC_HOTKEY_MAP_MAX];

int loongson_laptop_turn_on_backlight(void);
int loongson_laptop_turn_off_backlight(void);
static int loongson_laptop_backlight_update(struct backlight_device *bd);

/* 2. ACPI Helpers and device model */

static int acpi_evalf(acpi_handle handle, int *res, char *method, char *fmt, ...)
{
	char res_type;
	char *fmt0 = fmt;
	va_list ap;
	int success, quiet;
	acpi_status status;
	struct acpi_object_list params;
	struct acpi_buffer result, *resultp;
	union acpi_object in_objs[MAX_ACPI_ARGS], out_obj;

	if (!*fmt) {
		pr_err("acpi_evalf() called with empty format\n");
		return 0;
	}

	if (*fmt == 'q') {
		quiet = 1;
		fmt++;
	} else
		quiet = 0;

	res_type = *(fmt++);

	params.count = 0;
	params.pointer = &in_objs[0];

	va_start(ap, fmt);
	while (*fmt) {
		char c = *(fmt++);
		switch (c) {
		case 'd':	/* int */
			in_objs[params.count].integer.value = va_arg(ap, int);
			in_objs[params.count++].type = ACPI_TYPE_INTEGER;
			break;
			/* add more types as needed */
		default:
			pr_err("acpi_evalf() called with invalid format character '%c'\n", c);
			va_end(ap);
			return 0;
		}
	}
	va_end(ap);

	if (res_type != 'v') {
		result.length = sizeof(out_obj);
		result.pointer = &out_obj;
		resultp = &result;
	} else
		resultp = NULL;

	status = acpi_evaluate_object(handle, method, &params, resultp);

	switch (res_type) {
	case 'd':		/* int */
		success = (status == AE_OK && out_obj.type == ACPI_TYPE_INTEGER);
		if (success && res)
			*res = out_obj.integer.value;
		break;
	case 'v':		/* void */
		success = status == AE_OK;
		break;
		/* add more types as needed */
	default:
		pr_err("acpi_evalf() called with invalid format character '%c'\n", res_type);
		return 0;
	}

	if (!success && !quiet)
		pr_err("acpi_evalf(%s, %s, ...) failed: %s\n",
		       method, fmt0, acpi_format_exception(status));

	return success;
}

static int hotkey_status_get(int *status)
{
	if (!acpi_evalf(hotkey_handle, status, "GSWS", "d"))
		return -EIO;

	return 0;
}

static void dispatch_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	struct generic_sub_driver *sub_driver = data;

	if (!sub_driver || !sub_driver->notify)
		return;
	sub_driver->notify(sub_driver, event);
}

static int __init setup_acpi_notify(struct generic_sub_driver *sub_driver)
{
	acpi_status status;

	if (!*sub_driver->handle)
		return 0;

	sub_driver->device = acpi_fetch_acpi_dev(*sub_driver->handle);
	if (!sub_driver->device) {
		pr_err("acpi_fetch_acpi_dev(%s) failed\n", sub_driver->name);
		return -ENODEV;
	}

	sub_driver->device->driver_data = sub_driver;
	sprintf(acpi_device_class(sub_driver->device), "%s/%s",
		ACPI_LAPTOP_ACPI_EVENT_PREFIX, sub_driver->name);

	status = acpi_install_notify_handler(*sub_driver->handle,
			sub_driver->type, dispatch_acpi_notify, sub_driver);
	if (ACPI_FAILURE(status)) {
		if (status == AE_ALREADY_EXISTS) {
			pr_notice("Another device driver is already "
				  "handling %s events\n", sub_driver->name);
		} else {
			pr_err("acpi_install_notify_handler(%s) failed: %s\n",
			       sub_driver->name, acpi_format_exception(status));
		}
		return -ENODEV;
	}
	sub_driver->acpi_notify_installed = 1;

	return 0;
}

static int loongson_hotkey_suspend(struct device *dev)
{
	return 0;
}

static int loongson_hotkey_resume(struct device *dev)
{
	int status = 0;
	struct key_entry ke;
	struct backlight_device *bd;

	bd = backlight_device_get_by_type(BACKLIGHT_PLATFORM);
	if (bd) {
		loongson_laptop_backlight_update(bd) ?
		pr_warn("Loongson_backlight: resume brightness failed") :
		pr_info("Loongson_backlight: resume brightness %d\n", bd->props.brightness);
	}

	/*
	 * Only if the firmware supports SW_LID event model, we can handle the
	 * event. This is for the consideration of development board without EC.
	 */
	if (test_bit(SW_LID, generic_inputdev->swbit)) {
		if (hotkey_status_get(&status) < 0)
			return -EIO;
		/*
		 * The input device sw element records the last lid status.
		 * When the system is awakened by other wake-up sources,
		 * the lid event will also be reported. The judgment of
		 * adding SW_LID bit which in sw element can avoid this
		 * case.
		 *
		 * Input system will drop lid event when current lid event
		 * value and last lid status in the same. So laptop driver
		 * doesn't report repeated events.
		 *
		 * Lid status is generally 0, but hardware exception is
		 * considered. So add lid status confirmation.
		 */
		if (test_bit(SW_LID, generic_inputdev->sw) && !(status & (1 << SW_LID))) {
			ke.type = KE_SW;
			ke.sw.value = (u8)status;
			ke.sw.code = SW_LID;
			sparse_keymap_report_entry(generic_inputdev, &ke, 1, true);
		}
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(loongson_hotkey_pm,
		loongson_hotkey_suspend, loongson_hotkey_resume);

static int loongson_hotkey_probe(struct platform_device *pdev)
{
	hotkey_handle = ACPI_HANDLE(&pdev->dev);

	if (!hotkey_handle)
		return -ENODEV;

	return 0;
}

static const struct acpi_device_id loongson_device_ids[] = {
	{LOONGSON_ACPI_HKEY_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, loongson_device_ids);

static struct platform_driver loongson_hotkey_driver = {
	.probe		= loongson_hotkey_probe,
	.driver		= {
		.name	= "loongson-hotkey",
		.owner	= THIS_MODULE,
		.pm	= pm_ptr(&loongson_hotkey_pm),
		.acpi_match_table = loongson_device_ids,
	},
};

static int hotkey_map(void)
{
	u32 index;
	acpi_status status;
	struct acpi_buffer buf;
	union acpi_object *pack;

	buf.length = ACPI_ALLOCATE_BUFFER;
	status = acpi_evaluate_object_typed(hotkey_handle, "KMAP", NULL, &buf, ACPI_TYPE_PACKAGE);
	if (status != AE_OK) {
		pr_err("ACPI exception: %s\n", acpi_format_exception(status));
		return -1;
	}
	pack = buf.pointer;
	for (index = 0; index < pack->package.count; index++) {
		union acpi_object *element, *sub_pack;

		sub_pack = &pack->package.elements[index];

		element = &sub_pack->package.elements[0];
		hotkey_keycode_map[index].type = element->integer.value;
		element = &sub_pack->package.elements[1];
		hotkey_keycode_map[index].code = element->integer.value;
		element = &sub_pack->package.elements[2];
		hotkey_keycode_map[index].keycode = element->integer.value;
	}

	return 0;
}

static int hotkey_backlight_set(bool enable)
{
	if (!acpi_evalf(hotkey_handle, NULL, "VCBL", "vd", enable ? 1 : 0))
		return -EIO;

	return 0;
}

static int ec_get_brightness(void)
{
	int status = 0;

	if (!hotkey_handle)
		return -ENXIO;

	if (!acpi_evalf(hotkey_handle, &status, "ECBG", "d"))
		return -EIO;

	return status;
}

static int ec_set_brightness(int level)
{

	int ret = 0;

	if (!hotkey_handle)
		return -ENXIO;

	if (!acpi_evalf(hotkey_handle, NULL, "ECBS", "vd", level))
		ret = -EIO;

	return ret;
}

static int ec_backlight_level(u8 level)
{
	int status = 0;

	if (!hotkey_handle)
		return -ENXIO;

	if (!acpi_evalf(hotkey_handle, &status, "ECLL", "d"))
		return -EIO;

	if ((status < 0) || (level > status))
		return status;

	if (!acpi_evalf(hotkey_handle, &status, "ECSL", "d"))
		return -EIO;

	if ((status < 0) || (level < status))
		return status;

	return level;
}

static int loongson_laptop_backlight_update(struct backlight_device *bd)
{
	int lvl = ec_backlight_level(bd->props.brightness);

	if (lvl < 0)
		return -EIO;
	if (ec_set_brightness(lvl))
		return -EIO;

	return 0;
}

static int loongson_laptop_get_brightness(struct backlight_device *bd)
{
	int level;

	level = ec_get_brightness();
	if (level < 0)
		return -EIO;

	return level;
}

static const struct backlight_ops backlight_laptop_ops = {
	.update_status = loongson_laptop_backlight_update,
	.get_brightness = loongson_laptop_get_brightness,
};

static int laptop_backlight_register(void)
{
	int status = 0;
	struct backlight_properties props;

	memset(&props, 0, sizeof(props));

	if (!acpi_evalf(hotkey_handle, &status, "ECLL", "d"))
		return -EIO;

	props.brightness = 1;
	props.max_brightness = status;
	props.type = BACKLIGHT_PLATFORM;

	backlight_device_register("loongson_laptop",
				NULL, NULL, &backlight_laptop_ops, &props);

	return 0;
}

int loongson_laptop_turn_on_backlight(void)
{
	int status;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };

	arg0.integer.value = 1;
	status = acpi_evaluate_object(NULL, "\\BLSW", &args, NULL);
	if (ACPI_FAILURE(status)) {
		pr_info("Loongson lvds error: 0x%x\n", status);
		return -ENODEV;
	}

	return 0;
}

int loongson_laptop_turn_off_backlight(void)
{
	int status;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };

	arg0.integer.value = 0;
	status = acpi_evaluate_object(NULL, "\\BLSW", &args, NULL);
	if (ACPI_FAILURE(status)) {
		pr_info("Loongson lvds error: 0x%x\n", status);
		return -ENODEV;
	}

	return 0;
}

static int __init event_init(struct generic_sub_driver *sub_driver)
{
	int ret;

	ret = hotkey_map();
	if (ret < 0) {
		pr_err("Failed to parse keymap from DSDT\n");
		return ret;
	}

	ret = sparse_keymap_setup(generic_inputdev, hotkey_keycode_map, NULL);
	if (ret < 0) {
		pr_err("Failed to setup input device keymap\n");
		input_free_device(generic_inputdev);
		generic_inputdev = NULL;

		return ret;
	}

	/*
	 * This hotkey driver handle backlight event when
	 * acpi_video_get_backlight_type() gets acpi_backlight_vendor
	 */
	if (acpi_video_get_backlight_type() == acpi_backlight_vendor)
		hotkey_backlight_set(true);
	else
		hotkey_backlight_set(false);

	pr_info("ACPI: enabling firmware HKEY event interface...\n");

	return ret;
}

static void event_notify(struct generic_sub_driver *sub_driver, u32 event)
{
	int type, scan_code;
	struct key_entry *ke = NULL;

	scan_code = event & GENERIC_EVENT_CODE_MASK;
	type = (event & GENERIC_EVENT_TYPE_MASK) >> GENERIC_EVENT_TYPE_OFF;
	ke = sparse_keymap_entry_from_scancode(generic_inputdev, scan_code);
	if (ke) {
		if (type == KE_SW) {
			int status = 0;

			if (hotkey_status_get(&status) < 0)
				return;

			ke->sw.value = !!(status & (1 << ke->sw.code));
		}
		sparse_keymap_report_entry(generic_inputdev, ke, 1, true);
	}
}

/* 3. Infrastructure */

static void generic_subdriver_exit(struct generic_sub_driver *sub_driver);

static int __init generic_subdriver_init(struct generic_sub_driver *sub_driver)
{
	int ret;

	if (!sub_driver || !sub_driver->driver)
		return -EINVAL;

	ret = platform_driver_register(sub_driver->driver);
	if (ret)
		return -EINVAL;

	if (sub_driver->init) {
		ret = sub_driver->init(sub_driver);
		if (ret)
			goto err_out;
	}

	if (sub_driver->notify) {
		ret = setup_acpi_notify(sub_driver);
		if (ret == -ENODEV) {
			ret = 0;
			goto err_out;
		}
		if (ret < 0)
			goto err_out;
	}

	return 0;

err_out:
	generic_subdriver_exit(sub_driver);
	return ret;
}

static void generic_subdriver_exit(struct generic_sub_driver *sub_driver)
{

	if (sub_driver->acpi_notify_installed) {
		acpi_remove_notify_handler(*sub_driver->handle,
					   sub_driver->type, dispatch_acpi_notify);
		sub_driver->acpi_notify_installed = 0;
	}
	platform_driver_unregister(sub_driver->driver);
}

static struct generic_sub_driver generic_sub_drivers[] __refdata = {
	{
		.name = "hotkey",
		.init = event_init,
		.notify = event_notify,
		.handle = &hotkey_handle,
		.type = ACPI_DEVICE_NOTIFY,
		.driver = &loongson_hotkey_driver,
	},
};

static int __init generic_acpi_laptop_init(void)
{
	bool ec_found;
	int i, ret, status;

	if (acpi_disabled)
		return -ENODEV;

	/* The EC device is required */
	ec_found = acpi_dev_found(LOONGSON_ACPI_EC_HID);
	if (!ec_found)
		return -ENODEV;

	/* Enable SCI for EC */
	acpi_write_bit_register(ACPI_BITREG_SCI_ENABLE, 1);

	generic_inputdev = input_allocate_device();
	if (!generic_inputdev) {
		pr_err("Unable to allocate input device\n");
		return -ENOMEM;
	}

	/* Prepare input device, but don't register */
	generic_inputdev->name =
		"Loongson Generic Laptop/All-in-One Extra Buttons";
	generic_inputdev->phys = ACPI_LAPTOP_NAME "/input0";
	generic_inputdev->id.bustype = BUS_HOST;
	generic_inputdev->dev.parent = NULL;

	/* Init subdrivers */
	for (i = 0; i < ARRAY_SIZE(generic_sub_drivers); i++) {
		ret = generic_subdriver_init(&generic_sub_drivers[i]);
		if (ret < 0) {
			input_free_device(generic_inputdev);
			while (--i >= 0)
				generic_subdriver_exit(&generic_sub_drivers[i]);
			return ret;
		}
	}

	ret = input_register_device(generic_inputdev);
	if (ret < 0) {
		input_free_device(generic_inputdev);
		while (--i >= 0)
			generic_subdriver_exit(&generic_sub_drivers[i]);
		pr_err("Unable to register input device\n");
		return ret;
	}

	input_device_registered = 1;

	if (acpi_evalf(hotkey_handle, &status, "ECBG", "d")) {
		pr_info("Loongson Laptop used, init brightness is 0x%x\n", status);
		ret = laptop_backlight_register();
		if (ret < 0)
			pr_err("Loongson Laptop: laptop-backlight device register failed\n");
	}

	return 0;
}

static void __exit generic_acpi_laptop_exit(void)
{
	if (generic_inputdev) {
		if (input_device_registered)
			input_unregister_device(generic_inputdev);
		else
			input_free_device(generic_inputdev);
	}
}

module_init(generic_acpi_laptop_init);
module_exit(generic_acpi_laptop_exit);

MODULE_AUTHOR("Jianmin Lv <lvjianmin@loongson.cn>");
MODULE_AUTHOR("Huacai Chen <chenhuacai@loongson.cn>");
MODULE_DESCRIPTION("Loongson Laptop/All-in-One ACPI Driver");
MODULE_LICENSE("GPL");
