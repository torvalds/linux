// SPDX-License-Identifier: GPL-2.0
/*
 * Keyboard backlight LED driver for the Wilco Embedded Controller
 *
 * Copyright 2019 Google LLC
 *
 * Since the EC will never change the backlight level of its own accord,
 * we don't need to implement a brightness_get() method.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/platform_data/wilco-ec.h>
#include <linux/slab.h>

#define WILCO_EC_COMMAND_KBBL		0x75
#define WILCO_KBBL_MODE_FLAG_PWM	BIT(1)	/* Set brightness by percent. */
#define WILCO_KBBL_DEFAULT_BRIGHTNESS   0

struct wilco_keyboard_leds {
	struct wilco_ec_device *ec;
	struct led_classdev keyboard;
};

enum wilco_kbbl_subcommand {
	WILCO_KBBL_SUBCMD_GET_FEATURES = 0x00,
	WILCO_KBBL_SUBCMD_GET_STATE    = 0x01,
	WILCO_KBBL_SUBCMD_SET_STATE    = 0x02,
};

/**
 * struct wilco_keyboard_leds_msg - Message to/from EC for keyboard LED control.
 * @command: Always WILCO_EC_COMMAND_KBBL.
 * @status: Set by EC to 0 on success, 0xFF on failure.
 * @subcmd: One of enum wilco_kbbl_subcommand.
 * @reserved3: Should be 0.
 * @mode: Bit flags for used mode, we want to use WILCO_KBBL_MODE_FLAG_PWM.
 * @reserved5to8: Should be 0.
 * @percent: Brightness in 0-100. Only meaningful in PWM mode.
 * @reserved10to15: Should be 0.
 */
struct wilco_keyboard_leds_msg {
	u8 command;
	u8 status;
	u8 subcmd;
	u8 reserved3;
	u8 mode;
	u8 reserved5to8[4];
	u8 percent;
	u8 reserved10to15[6];
} __packed;

/* Send a request, get a response, and check that the response is good. */
static int send_kbbl_msg(struct wilco_ec_device *ec,
			 struct wilco_keyboard_leds_msg *request,
			 struct wilco_keyboard_leds_msg *response)
{
	struct wilco_ec_message msg;
	int ret;

	memset(&msg, 0, sizeof(msg));
	msg.type = WILCO_EC_MSG_LEGACY;
	msg.request_data = request;
	msg.request_size = sizeof(*request);
	msg.response_data = response;
	msg.response_size = sizeof(*response);

	ret = wilco_ec_mailbox(ec, &msg);
	if (ret < 0) {
		dev_err(ec->dev,
			"Failed sending keyboard LEDs command: %d\n", ret);
		return ret;
	}

	return 0;
}

static int set_kbbl(struct wilco_ec_device *ec, enum led_brightness brightness)
{
	struct wilco_keyboard_leds_msg request;
	struct wilco_keyboard_leds_msg response;
	int ret;

	memset(&request, 0, sizeof(request));
	request.command = WILCO_EC_COMMAND_KBBL;
	request.subcmd  = WILCO_KBBL_SUBCMD_SET_STATE;
	request.mode    = WILCO_KBBL_MODE_FLAG_PWM;
	request.percent = brightness;

	ret = send_kbbl_msg(ec, &request, &response);
	if (ret < 0)
		return ret;

	if (response.status) {
		dev_err(ec->dev,
			"EC reported failure sending keyboard LEDs command: %d\n",
			response.status);
		return -EIO;
	}

	return 0;
}

static int kbbl_exist(struct wilco_ec_device *ec, bool *exists)
{
	struct wilco_keyboard_leds_msg request;
	struct wilco_keyboard_leds_msg response;
	int ret;

	memset(&request, 0, sizeof(request));
	request.command = WILCO_EC_COMMAND_KBBL;
	request.subcmd  = WILCO_KBBL_SUBCMD_GET_FEATURES;

	ret = send_kbbl_msg(ec, &request, &response);
	if (ret < 0)
		return ret;

	*exists = response.status != 0xFF;

	return 0;
}

/**
 * kbbl_init() - Initialize the state of the keyboard backlight.
 * @ec: EC device to talk to.
 *
 * Gets the current brightness, ensuring that the BIOS already initialized the
 * backlight to PWM mode. If not in PWM mode, then the current brightness is
 * meaningless, so set the brightness to WILCO_KBBL_DEFAULT_BRIGHTNESS.
 *
 * Return: Final brightness of the keyboard, or negative error code on failure.
 */
static int kbbl_init(struct wilco_ec_device *ec)
{
	struct wilco_keyboard_leds_msg request;
	struct wilco_keyboard_leds_msg response;
	int ret;

	memset(&request, 0, sizeof(request));
	request.command = WILCO_EC_COMMAND_KBBL;
	request.subcmd  = WILCO_KBBL_SUBCMD_GET_STATE;

	ret = send_kbbl_msg(ec, &request, &response);
	if (ret < 0)
		return ret;

	if (response.status) {
		dev_err(ec->dev,
			"EC reported failure sending keyboard LEDs command: %d\n",
			response.status);
		return -EIO;
	}

	if (response.mode & WILCO_KBBL_MODE_FLAG_PWM)
		return response.percent;

	ret = set_kbbl(ec, WILCO_KBBL_DEFAULT_BRIGHTNESS);
	if (ret < 0)
		return ret;

	return WILCO_KBBL_DEFAULT_BRIGHTNESS;
}

static int wilco_keyboard_leds_set(struct led_classdev *cdev,
				   enum led_brightness brightness)
{
	struct wilco_keyboard_leds *wkl =
		container_of(cdev, struct wilco_keyboard_leds, keyboard);
	return set_kbbl(wkl->ec, brightness);
}

int wilco_keyboard_leds_init(struct wilco_ec_device *ec)
{
	struct wilco_keyboard_leds *wkl;
	bool leds_exist;
	int ret;

	ret = kbbl_exist(ec, &leds_exist);
	if (ret < 0) {
		dev_err(ec->dev,
			"Failed checking keyboard LEDs support: %d\n", ret);
		return ret;
	}
	if (!leds_exist)
		return 0;

	wkl = devm_kzalloc(ec->dev, sizeof(*wkl), GFP_KERNEL);
	if (!wkl)
		return -ENOMEM;

	wkl->ec = ec;
	wkl->keyboard.name = "platform::kbd_backlight";
	wkl->keyboard.max_brightness = 100;
	wkl->keyboard.flags = LED_CORE_SUSPENDRESUME;
	wkl->keyboard.brightness_set_blocking = wilco_keyboard_leds_set;
	ret = kbbl_init(ec);
	if (ret < 0)
		return ret;
	wkl->keyboard.brightness = ret;

	return devm_led_classdev_register(ec->dev, &wkl->keyboard);
}
