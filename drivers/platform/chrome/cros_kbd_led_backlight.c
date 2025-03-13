// SPDX-License-Identifier: GPL-2.0+
// Keyboard backlight LED driver for ChromeOS
//
// Copyright (C) 2012 Google, Inc.

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mfd/core.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

struct keyboard_led {
	struct led_classdev cdev;
	struct cros_ec_device *ec;
};

/**
 * struct keyboard_led_drvdata - keyboard LED driver data.
 * @init:			Init function.
 * @brightness_get:		Get LED brightness level.
 * @brightness_set:		Set LED brightness level.  Must not sleep.
 * @brightness_set_blocking:	Set LED brightness level.  It can block the
 *				caller for the time required for accessing a
 *				LED device register
 * @max_brightness:		Maximum brightness.
 *
 * See struct led_classdev in include/linux/leds.h for more details.
 */
struct keyboard_led_drvdata {
	int (*init)(struct platform_device *pdev);

	enum led_brightness (*brightness_get)(struct led_classdev *led_cdev);

	void (*brightness_set)(struct led_classdev *led_cdev,
			       enum led_brightness brightness);
	int (*brightness_set_blocking)(struct led_classdev *led_cdev,
				       enum led_brightness brightness);

	enum led_brightness max_brightness;
};

#define KEYBOARD_BACKLIGHT_MAX 100

#ifdef CONFIG_ACPI

/* Keyboard LED ACPI Device must be defined in firmware */
#define ACPI_KEYBOARD_BACKLIGHT_DEVICE	"\\_SB.KBLT"
#define ACPI_KEYBOARD_BACKLIGHT_READ	ACPI_KEYBOARD_BACKLIGHT_DEVICE ".KBQC"
#define ACPI_KEYBOARD_BACKLIGHT_WRITE	ACPI_KEYBOARD_BACKLIGHT_DEVICE ".KBCM"

static void keyboard_led_set_brightness_acpi(struct led_classdev *cdev,
					     enum led_brightness brightness)
{
	union acpi_object param;
	struct acpi_object_list input;
	acpi_status status;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = brightness;
	input.count = 1;
	input.pointer = &param;

	status = acpi_evaluate_object(NULL, ACPI_KEYBOARD_BACKLIGHT_WRITE,
				      &input, NULL);
	if (ACPI_FAILURE(status))
		dev_err(cdev->dev, "Error setting keyboard LED value: %d\n",
			status);
}

static enum led_brightness
keyboard_led_get_brightness_acpi(struct led_classdev *cdev)
{
	unsigned long long brightness;
	acpi_status status;

	status = acpi_evaluate_integer(NULL, ACPI_KEYBOARD_BACKLIGHT_READ,
				       NULL, &brightness);
	if (ACPI_FAILURE(status)) {
		dev_err(cdev->dev, "Error getting keyboard LED value: %d\n",
			status);
		return -EIO;
	}

	return brightness;
}

static int keyboard_led_init_acpi(struct platform_device *pdev)
{
	acpi_handle handle;
	acpi_status status;

	/* Look for the keyboard LED ACPI Device */
	status = acpi_get_handle(ACPI_ROOT_OBJECT,
				 ACPI_KEYBOARD_BACKLIGHT_DEVICE,
				 &handle);
	if (ACPI_FAILURE(status)) {
		dev_err(&pdev->dev, "Unable to find ACPI device %s: %d\n",
			ACPI_KEYBOARD_BACKLIGHT_DEVICE, status);
		return -ENXIO;
	}

	return 0;
}

static const struct keyboard_led_drvdata keyboard_led_drvdata_acpi = {
	.init = keyboard_led_init_acpi,
	.brightness_set = keyboard_led_set_brightness_acpi,
	.brightness_get = keyboard_led_get_brightness_acpi,
	.max_brightness = KEYBOARD_BACKLIGHT_MAX,
};

#endif /* CONFIG_ACPI */

#if IS_ENABLED(CONFIG_MFD_CROS_EC_DEV)
static int keyboard_led_init_ec_pwm_mfd(struct platform_device *pdev)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pdev->dev.parent);
	struct cros_ec_device *cros_ec = ec_dev->ec_dev;
	struct keyboard_led *keyboard_led = platform_get_drvdata(pdev);

	keyboard_led->ec = cros_ec;

	return 0;
}

static int
keyboard_led_set_brightness_ec_pwm(struct led_classdev *cdev,
				   enum led_brightness brightness)
{
	struct {
		struct cros_ec_command msg;
		struct ec_params_pwm_set_keyboard_backlight params;
	} __packed buf;
	struct ec_params_pwm_set_keyboard_backlight *params = &buf.params;
	struct cros_ec_command *msg = &buf.msg;
	struct keyboard_led *keyboard_led = container_of(cdev, struct keyboard_led, cdev);

	memset(&buf, 0, sizeof(buf));

	msg->command = EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT;
	msg->outsize = sizeof(*params);

	params->percent = brightness;

	return cros_ec_cmd_xfer_status(keyboard_led->ec, msg);
}

static enum led_brightness
keyboard_led_get_brightness_ec_pwm(struct led_classdev *cdev)
{
	struct {
		struct cros_ec_command msg;
		struct ec_response_pwm_get_keyboard_backlight resp;
	} __packed buf;
	struct ec_response_pwm_get_keyboard_backlight *resp = &buf.resp;
	struct cros_ec_command *msg = &buf.msg;
	struct keyboard_led *keyboard_led = container_of(cdev, struct keyboard_led, cdev);
	int ret;

	memset(&buf, 0, sizeof(buf));

	msg->command = EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT;
	msg->insize = sizeof(*resp);

	ret = cros_ec_cmd_xfer_status(keyboard_led->ec, msg);
	if (ret < 0)
		return ret;

	return resp->percent;
}

static const struct keyboard_led_drvdata keyboard_led_drvdata_ec_pwm_mfd = {
	.init = keyboard_led_init_ec_pwm_mfd,
	.brightness_set_blocking = keyboard_led_set_brightness_ec_pwm,
	.brightness_get = keyboard_led_get_brightness_ec_pwm,
	.max_brightness = KEYBOARD_BACKLIGHT_MAX,
};

#else /* IS_ENABLED(CONFIG_MFD_CROS_EC_DEV) */

static const struct keyboard_led_drvdata keyboard_led_drvdata_ec_pwm_mfd = {};

#endif /* IS_ENABLED(CONFIG_MFD_CROS_EC_DEV) */

static int keyboard_led_is_mfd_device(struct platform_device *pdev)
{
	return IS_ENABLED(CONFIG_MFD_CROS_EC_DEV) && mfd_get_cell(pdev);
}

static int keyboard_led_probe(struct platform_device *pdev)
{
	const struct keyboard_led_drvdata *drvdata;
	struct keyboard_led *keyboard_led;
	int err;

	if (keyboard_led_is_mfd_device(pdev))
		drvdata = &keyboard_led_drvdata_ec_pwm_mfd;
	else
		drvdata = device_get_match_data(&pdev->dev);
	if (!drvdata)
		return -EINVAL;

	keyboard_led = devm_kzalloc(&pdev->dev, sizeof(*keyboard_led), GFP_KERNEL);
	if (!keyboard_led)
		return -ENOMEM;
	platform_set_drvdata(pdev, keyboard_led);

	if (drvdata->init) {
		err = drvdata->init(pdev);
		if (err)
			return err;
	}

	keyboard_led->cdev.name = "chromeos::kbd_backlight";
	keyboard_led->cdev.flags |= LED_CORE_SUSPENDRESUME | LED_REJECT_NAME_CONFLICT;
	keyboard_led->cdev.max_brightness = drvdata->max_brightness;
	keyboard_led->cdev.brightness_set = drvdata->brightness_set;
	keyboard_led->cdev.brightness_set_blocking = drvdata->brightness_set_blocking;
	keyboard_led->cdev.brightness_get = drvdata->brightness_get;

	err = devm_led_classdev_register(&pdev->dev, &keyboard_led->cdev);
	if (err == -EEXIST) /* Already bound via other mechanism */
		return -ENODEV;
	return err;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id keyboard_led_acpi_match[] = {
	{ "GOOG0002", (kernel_ulong_t)&keyboard_led_drvdata_acpi },
	{ }
};
MODULE_DEVICE_TABLE(acpi, keyboard_led_acpi_match);
#endif

static const struct platform_device_id keyboard_led_id[] = {
	{ "cros-keyboard-leds", 0 },
	{}
};
MODULE_DEVICE_TABLE(platform, keyboard_led_id);

static struct platform_driver keyboard_led_driver = {
	.driver		= {
		.name	= "cros-keyboard-leds",
		.acpi_match_table = ACPI_PTR(keyboard_led_acpi_match),
	},
	.probe		= keyboard_led_probe,
	.id_table	= keyboard_led_id,
};
module_platform_driver(keyboard_led_driver);

MODULE_AUTHOR("Simon Que <sque@chromium.org>");
MODULE_DESCRIPTION("ChromeOS Keyboard backlight LED Driver");
MODULE_LICENSE("GPL");
