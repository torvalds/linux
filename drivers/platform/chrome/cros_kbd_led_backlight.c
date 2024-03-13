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

#if IS_ENABLED(CONFIG_CROS_EC)

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

static int keyboard_led_init_ec_pwm(struct platform_device *pdev)
{
	struct keyboard_led *keyboard_led = platform_get_drvdata(pdev);

	keyboard_led->ec = dev_get_drvdata(pdev->dev.parent);
	if (!keyboard_led->ec) {
		dev_err(&pdev->dev, "no parent EC device\n");
		return -EINVAL;
	}

	return 0;
}

static const __maybe_unused struct keyboard_led_drvdata keyboard_led_drvdata_ec_pwm = {
	.init = keyboard_led_init_ec_pwm,
	.brightness_set_blocking = keyboard_led_set_brightness_ec_pwm,
	.brightness_get = keyboard_led_get_brightness_ec_pwm,
	.max_brightness = KEYBOARD_BACKLIGHT_MAX,
};

#else /* IS_ENABLED(CONFIG_CROS_EC) */

static const __maybe_unused struct keyboard_led_drvdata keyboard_led_drvdata_ec_pwm = {};

#endif /* IS_ENABLED(CONFIG_CROS_EC) */

static int keyboard_led_probe(struct platform_device *pdev)
{
	const struct keyboard_led_drvdata *drvdata;
	struct keyboard_led *keyboard_led;
	int error;

	drvdata = device_get_match_data(&pdev->dev);
	if (!drvdata)
		return -EINVAL;

	keyboard_led = devm_kzalloc(&pdev->dev, sizeof(*keyboard_led), GFP_KERNEL);
	if (!keyboard_led)
		return -ENOMEM;
	platform_set_drvdata(pdev, keyboard_led);

	if (drvdata->init) {
		error = drvdata->init(pdev);
		if (error)
			return error;
	}

	keyboard_led->cdev.name = "chromeos::kbd_backlight";
	keyboard_led->cdev.flags |= LED_CORE_SUSPENDRESUME;
	keyboard_led->cdev.max_brightness = drvdata->max_brightness;
	keyboard_led->cdev.brightness_set = drvdata->brightness_set;
	keyboard_led->cdev.brightness_set_blocking = drvdata->brightness_set_blocking;
	keyboard_led->cdev.brightness_get = drvdata->brightness_get;

	error = devm_led_classdev_register(&pdev->dev, &keyboard_led->cdev);
	if (error)
		return error;

	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id keyboard_led_acpi_match[] = {
	{ "GOOG0002", (kernel_ulong_t)&keyboard_led_drvdata_acpi },
	{ }
};
MODULE_DEVICE_TABLE(acpi, keyboard_led_acpi_match);
#endif

#ifdef CONFIG_OF
static const struct of_device_id keyboard_led_of_match[] = {
	{
		.compatible = "google,cros-kbd-led-backlight",
		.data = &keyboard_led_drvdata_ec_pwm,
	},
	{}
};
MODULE_DEVICE_TABLE(of, keyboard_led_of_match);
#endif

static struct platform_driver keyboard_led_driver = {
	.driver		= {
		.name	= "chromeos-keyboard-leds",
		.acpi_match_table = ACPI_PTR(keyboard_led_acpi_match),
		.of_match_table = of_match_ptr(keyboard_led_of_match),
	},
	.probe		= keyboard_led_probe,
};
module_platform_driver(keyboard_led_driver);

MODULE_AUTHOR("Simon Que <sque@chromium.org>");
MODULE_DESCRIPTION("ChromeOS Keyboard backlight LED Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:chromeos-keyboard-leds");
