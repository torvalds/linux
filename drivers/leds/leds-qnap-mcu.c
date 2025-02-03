// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for LEDs found on QNAP MCU devices
 *
 * Copyright (C) 2024 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/leds.h>
#include <linux/mfd/qnap-mcu.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <uapi/linux/uleds.h>

enum qnap_mcu_err_led_mode {
	QNAP_MCU_ERR_LED_ON = 0,
	QNAP_MCU_ERR_LED_OFF = 1,
	QNAP_MCU_ERR_LED_BLINK_FAST = 2,
	QNAP_MCU_ERR_LED_BLINK_SLOW = 3,
};

struct qnap_mcu_err_led {
	struct qnap_mcu *mcu;
	struct led_classdev cdev;
	char name[LED_MAX_NAME_SIZE];
	u8 num;
	u8 mode;
};

static inline struct qnap_mcu_err_led *
		cdev_to_qnap_mcu_err_led(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct qnap_mcu_err_led, cdev);
}

static int qnap_mcu_err_led_set(struct led_classdev *led_cdev,
				enum led_brightness brightness)
{
	struct qnap_mcu_err_led *err_led = cdev_to_qnap_mcu_err_led(led_cdev);
	u8 cmd[] = { '@', 'R', '0' + err_led->num, '0' };

	/* Don't disturb a possible set blink-mode if LED stays on */
	if (brightness != 0 && err_led->mode >= QNAP_MCU_ERR_LED_BLINK_FAST)
		return 0;

	err_led->mode = brightness ? QNAP_MCU_ERR_LED_ON : QNAP_MCU_ERR_LED_OFF;
	cmd[3] = '0' + err_led->mode;

	return qnap_mcu_exec_with_ack(err_led->mcu, cmd, sizeof(cmd));
}

static int qnap_mcu_err_led_blink_set(struct led_classdev *led_cdev,
				      unsigned long *delay_on,
				      unsigned long *delay_off)
{
	struct qnap_mcu_err_led *err_led = cdev_to_qnap_mcu_err_led(led_cdev);
	u8 cmd[] = { '@', 'R', '0' + err_led->num, '0' };

	/* LED is off, nothing to do */
	if (err_led->mode == QNAP_MCU_ERR_LED_OFF)
		return 0;

	if (*delay_on < 500) {
		*delay_on = 100;
		*delay_off = 100;
		err_led->mode = QNAP_MCU_ERR_LED_BLINK_FAST;
	} else {
		*delay_on = 500;
		*delay_off = 500;
		err_led->mode = QNAP_MCU_ERR_LED_BLINK_SLOW;
	}

	cmd[3] = '0' + err_led->mode;

	return qnap_mcu_exec_with_ack(err_led->mcu, cmd, sizeof(cmd));
}

static int qnap_mcu_register_err_led(struct device *dev, struct qnap_mcu *mcu, int num_err_led)
{
	struct qnap_mcu_err_led *err_led;
	int ret;

	err_led = devm_kzalloc(dev, sizeof(*err_led), GFP_KERNEL);
	if (!err_led)
		return -ENOMEM;

	err_led->mcu = mcu;
	err_led->num = num_err_led;
	err_led->mode = QNAP_MCU_ERR_LED_OFF;

	scnprintf(err_led->name, LED_MAX_NAME_SIZE, "hdd%d:red:status", num_err_led + 1);
	err_led->cdev.name = err_led->name;

	err_led->cdev.brightness_set_blocking = qnap_mcu_err_led_set;
	err_led->cdev.blink_set = qnap_mcu_err_led_blink_set;
	err_led->cdev.brightness = 0;
	err_led->cdev.max_brightness = 1;

	ret = devm_led_classdev_register(dev, &err_led->cdev);
	if (ret)
		return ret;

	return qnap_mcu_err_led_set(&err_led->cdev, 0);
}

enum qnap_mcu_usb_led_mode {
	QNAP_MCU_USB_LED_ON = 1,
	QNAP_MCU_USB_LED_OFF = 3,
	QNAP_MCU_USB_LED_BLINK = 2,
};

struct qnap_mcu_usb_led {
	struct qnap_mcu *mcu;
	struct led_classdev cdev;
	u8 mode;
};

static inline struct qnap_mcu_usb_led *
		cdev_to_qnap_mcu_usb_led(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct qnap_mcu_usb_led, cdev);
}

static int qnap_mcu_usb_led_set(struct led_classdev *led_cdev,
				enum led_brightness brightness)
{
	struct qnap_mcu_usb_led *usb_led = cdev_to_qnap_mcu_usb_led(led_cdev);
	u8 cmd[] = { '@', 'C', 0 };

	/* Don't disturb a possible set blink-mode if LED stays on */
	if (brightness != 0 && usb_led->mode == QNAP_MCU_USB_LED_BLINK)
		return 0;

	usb_led->mode = brightness ? QNAP_MCU_USB_LED_ON : QNAP_MCU_USB_LED_OFF;

	/*
	 * Byte 3 is shared between the usb led target on/off/blink
	 * and also the buzzer control (in the input driver)
	 */
	cmd[2] = 'D' + usb_led->mode;

	return qnap_mcu_exec_with_ack(usb_led->mcu, cmd, sizeof(cmd));
}

static int qnap_mcu_usb_led_blink_set(struct led_classdev *led_cdev,
				      unsigned long *delay_on,
				      unsigned long *delay_off)
{
	struct qnap_mcu_usb_led *usb_led = cdev_to_qnap_mcu_usb_led(led_cdev);
	u8 cmd[] = { '@', 'C', 0 };

	/* LED is off, nothing to do */
	if (usb_led->mode == QNAP_MCU_USB_LED_OFF)
		return 0;

	*delay_on = 250;
	*delay_off = 250;
	usb_led->mode = QNAP_MCU_USB_LED_BLINK;

	/*
	 * Byte 3 is shared between the USB LED target on/off/blink
	 * and also the buzzer control (in the input driver)
	 */
	cmd[2] = 'D' + usb_led->mode;

	return qnap_mcu_exec_with_ack(usb_led->mcu, cmd, sizeof(cmd));
}

static int qnap_mcu_register_usb_led(struct device *dev, struct qnap_mcu *mcu)
{
	struct qnap_mcu_usb_led *usb_led;
	int ret;

	usb_led = devm_kzalloc(dev, sizeof(*usb_led), GFP_KERNEL);
	if (!usb_led)
		return -ENOMEM;

	usb_led->mcu = mcu;
	usb_led->mode = QNAP_MCU_USB_LED_OFF;
	usb_led->cdev.name = "usb:blue:disk";
	usb_led->cdev.brightness_set_blocking = qnap_mcu_usb_led_set;
	usb_led->cdev.blink_set = qnap_mcu_usb_led_blink_set;
	usb_led->cdev.brightness = 0;
	usb_led->cdev.max_brightness = 1;

	ret = devm_led_classdev_register(dev, &usb_led->cdev);
	if (ret)
		return ret;

	return qnap_mcu_usb_led_set(&usb_led->cdev, 0);
}

static int qnap_mcu_leds_probe(struct platform_device *pdev)
{
	struct qnap_mcu *mcu = dev_get_drvdata(pdev->dev.parent);
	const struct qnap_mcu_variant *variant = pdev->dev.platform_data;
	int ret;

	for (int i = 0; i < variant->num_drives; i++) {
		ret = qnap_mcu_register_err_led(&pdev->dev, mcu, i);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					"failed to register error LED %d\n", i);
	}

	if (variant->usb_led) {
		ret = qnap_mcu_register_usb_led(&pdev->dev, mcu);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					"failed to register USB LED\n");
	}

	return 0;
}

static struct platform_driver qnap_mcu_leds_driver = {
	.probe = qnap_mcu_leds_probe,
	.driver = {
		.name = "qnap-mcu-leds",
	},
};
module_platform_driver(qnap_mcu_leds_driver);

MODULE_ALIAS("platform:qnap-mcu-leds");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("QNAP MCU LEDs driver");
MODULE_LICENSE("GPL");
