// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Dell AIO Serial Backlight Driver
 *
 * Copyright (C) 2024 Hans de Goede <hansg@kernel.org>
 * Copyright (C) 2017 AceLan Kao <acelan.kao@canonical.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/serdev.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <acpi/video.h>
#include "../serdev_helpers.h"

/* The backlight controller must respond within 1 second */
#define DELL_BL_TIMEOUT		msecs_to_jiffies(1000)
#define DELL_BL_MAX_BRIGHTNESS	100

/* Defines for the commands send to the controller */

/* 1st byte Start Of Frame 3 MSB bits: cmd-len + 01010 SOF marker */
#define DELL_SOF(len)			(((len) << 5) | 0x0a)
#define GET_CMD_LEN			3
#define SET_CMD_LEN			4

/* 2nd byte command */
#define CMD_GET_VERSION			0x06
#define CMD_SET_BRIGHTNESS		0x0b
#define CMD_GET_BRIGHTNESS		0x0c
#define CMD_SET_BL_POWER		0x0e

/* Indexes and other defines for response received from the controller */
#define RESP_LEN			0
#define RESP_CMD			1 /* Echo of CMD byte from command */
#define RESP_DATA			2 /* Start of received data */

#define SET_RESP_LEN			3
#define GET_RESP_LEN			4
#define MIN_RESP_LEN			3
#define MAX_RESP_LEN			80

struct dell_uart_backlight {
	struct mutex mutex;
	wait_queue_head_t wait_queue;
	struct device *dev;
	struct backlight_device *bl;
	u8 *resp;
	u8 resp_idx;
	u8 resp_len;
	u8 resp_max_len;
	u8 pending_cmd;
	int status;
	int power;
};

/* Checksum: SUM(Length and Cmd and Data) xor 0xFF */
static u8 dell_uart_checksum(u8 *buf, int len)
{
	u8 val = 0;

	while (len-- > 0)
		val += buf[len];

	return val ^ 0xff;
}

static int dell_uart_bl_command(struct dell_uart_backlight *dell_bl,
				const u8 *cmd, int cmd_len,
				u8 *resp, int resp_max_len)
{
	int ret;

	ret = mutex_lock_killable(&dell_bl->mutex);
	if (ret)
		return ret;

	dell_bl->status = -EBUSY;
	dell_bl->resp = resp;
	dell_bl->resp_idx = 0;
	dell_bl->resp_len = -1; /* Invalid / unset */
	dell_bl->resp_max_len = resp_max_len;
	dell_bl->pending_cmd = cmd[1];

	/* The TTY buffer should be big enough to take the entire cmd in one go */
	ret = serdev_device_write_buf(to_serdev_device(dell_bl->dev), cmd, cmd_len);
	if (ret != cmd_len) {
		dev_err(dell_bl->dev, "Error writing command: %d\n", ret);
		dell_bl->status = (ret < 0) ? ret : -EIO;
		goto out;
	}

	ret = wait_event_timeout(dell_bl->wait_queue, dell_bl->status != -EBUSY,
				 DELL_BL_TIMEOUT);
	if (ret == 0) {
		dev_err(dell_bl->dev, "Timed out waiting for response.\n");
		/* Clear busy status to discard bytes received after this */
		dell_bl->status = -ETIMEDOUT;
	}

out:
	mutex_unlock(&dell_bl->mutex);
	return dell_bl->status;
}

static int dell_uart_set_brightness(struct dell_uart_backlight *dell_bl, int brightness)
{
	u8 set_brightness[SET_CMD_LEN], resp[SET_RESP_LEN];

	set_brightness[0] = DELL_SOF(SET_CMD_LEN);
	set_brightness[1] = CMD_SET_BRIGHTNESS;
	set_brightness[2] = brightness;
	set_brightness[3] = dell_uart_checksum(set_brightness, 3);

	return dell_uart_bl_command(dell_bl, set_brightness, SET_CMD_LEN, resp, SET_RESP_LEN);
}

static int dell_uart_get_brightness(struct dell_uart_backlight *dell_bl)
{
	struct device *dev = dell_bl->dev;
	u8 get_brightness[GET_CMD_LEN], resp[GET_RESP_LEN];
	int ret;

	get_brightness[0] = DELL_SOF(GET_CMD_LEN);
	get_brightness[1] = CMD_GET_BRIGHTNESS;
	get_brightness[2] = dell_uart_checksum(get_brightness, 2);

	ret = dell_uart_bl_command(dell_bl, get_brightness, GET_CMD_LEN, resp, GET_RESP_LEN);
	if (ret)
		return ret;

	if (resp[RESP_LEN] != GET_RESP_LEN) {
		dev_err(dev, "Unexpected get brightness response length: %d\n", resp[RESP_LEN]);
		return -EIO;
	}

	if (resp[RESP_DATA] > DELL_BL_MAX_BRIGHTNESS) {
		dev_err(dev, "Unexpected get brightness response: %d\n", resp[RESP_DATA]);
		return -EIO;
	}

	return resp[RESP_DATA];
}

static int dell_uart_set_bl_power(struct dell_uart_backlight *dell_bl, int power)
{
	u8 set_power[SET_CMD_LEN], resp[SET_RESP_LEN];
	int ret;

	set_power[0] = DELL_SOF(SET_CMD_LEN);
	set_power[1] = CMD_SET_BL_POWER;
	set_power[2] = (power == FB_BLANK_UNBLANK) ? 1 : 0;
	set_power[3] = dell_uart_checksum(set_power, 3);

	ret = dell_uart_bl_command(dell_bl, set_power, SET_CMD_LEN, resp, SET_RESP_LEN);
	if (ret)
		return ret;

	dell_bl->power = power;
	return 0;
}

/*
 * There is no command to get backlight power status,
 * so we set the backlight power to "on" while initializing,
 * and then track and report its status by power variable.
 */
static int dell_uart_get_bl_power(struct dell_uart_backlight *dell_bl)
{
	return dell_bl->power;
}

static int dell_uart_update_status(struct backlight_device *bd)
{
	struct dell_uart_backlight *dell_bl = bl_get_data(bd);
	int ret;

	ret = dell_uart_set_brightness(dell_bl, bd->props.brightness);
	if (ret)
		return ret;

	if (bd->props.power != dell_uart_get_bl_power(dell_bl))
		return dell_uart_set_bl_power(dell_bl, bd->props.power);

	return 0;
}

static int dell_uart_get_brightness_op(struct backlight_device *bd)
{
	return dell_uart_get_brightness(bl_get_data(bd));
}

static const struct backlight_ops dell_uart_backlight_ops = {
	.update_status = dell_uart_update_status,
	.get_brightness = dell_uart_get_brightness_op,
};

static size_t dell_uart_bl_receive(struct serdev_device *serdev, const u8 *data, size_t len)
{
	struct dell_uart_backlight *dell_bl = serdev_device_get_drvdata(serdev);
	size_t i;
	u8 csum;

	dev_dbg(dell_bl->dev, "Recv: %*ph\n", (int)len, data);

	/* Throw away unexpected bytes / remainder of response after an error */
	if (dell_bl->status != -EBUSY) {
		dev_warn(dell_bl->dev, "Bytes received out of band, dropping them.\n");
		return len;
	}

	i = 0;
	while (i < len && dell_bl->resp_idx != dell_bl->resp_len) {
		dell_bl->resp[dell_bl->resp_idx] = data[i++];

		switch (dell_bl->resp_idx) {
		case RESP_LEN: /* Length byte */
			dell_bl->resp_len = dell_bl->resp[RESP_LEN];
			if (dell_bl->resp_len < MIN_RESP_LEN ||
			    dell_bl->resp_len > dell_bl->resp_max_len) {
				dev_err(dell_bl->dev, "Response length %d out if range %d - %d\n",
					dell_bl->resp_len, MIN_RESP_LEN, dell_bl->resp_max_len);
				dell_bl->status = -EIO;
				goto wakeup;
			}
			break;
		case RESP_CMD: /* CMD byte */
			if (dell_bl->resp[RESP_CMD] != dell_bl->pending_cmd) {
				dev_err(dell_bl->dev, "Response cmd 0x%02x != pending 0x%02x\n",
					dell_bl->resp[RESP_CMD], dell_bl->pending_cmd);
				dell_bl->status = -EIO;
				goto wakeup;
			}
			break;
		}
		dell_bl->resp_idx++;
	}

	if (dell_bl->resp_idx != dell_bl->resp_len)
		return len; /* Response not complete yet */

	csum = dell_uart_checksum(dell_bl->resp, dell_bl->resp_len - 1);
	if (dell_bl->resp[dell_bl->resp_len - 1] == csum) {
		dell_bl->status = 0; /* Success */
	} else {
		dev_err(dell_bl->dev, "Checksum mismatch got 0x%02x expected 0x%02x\n",
			dell_bl->resp[dell_bl->resp_len - 1], csum);
		dell_bl->status = -EIO;
	}
wakeup:
	wake_up(&dell_bl->wait_queue);
	return i;
}

static const struct serdev_device_ops dell_uart_bl_serdev_ops = {
	.receive_buf = dell_uart_bl_receive,
	.write_wakeup = serdev_device_write_wakeup,
};

static int dell_uart_bl_serdev_probe(struct serdev_device *serdev)
{
	u8 get_version[GET_CMD_LEN], resp[MAX_RESP_LEN];
	struct backlight_properties props = {};
	struct dell_uart_backlight *dell_bl;
	struct device *dev = &serdev->dev;
	int ret;

	dell_bl = devm_kzalloc(dev, sizeof(*dell_bl), GFP_KERNEL);
	if (!dell_bl)
		return -ENOMEM;

	mutex_init(&dell_bl->mutex);
	init_waitqueue_head(&dell_bl->wait_queue);
	dell_bl->dev = dev;

	ret = devm_serdev_device_open(dev, serdev);
	if (ret)
		return dev_err_probe(dev, ret, "opening UART device\n");

	/* 9600 bps, no flow control, these are the default but set them to be sure */
	serdev_device_set_baudrate(serdev, 9600);
	serdev_device_set_flow_control(serdev, false);
	serdev_device_set_drvdata(serdev, dell_bl);
	serdev_device_set_client_ops(serdev, &dell_uart_bl_serdev_ops);

	get_version[0] = DELL_SOF(GET_CMD_LEN);
	get_version[1] = CMD_GET_VERSION;
	get_version[2] = dell_uart_checksum(get_version, 2);

	ret = dell_uart_bl_command(dell_bl, get_version, GET_CMD_LEN, resp, MAX_RESP_LEN);
	if (ret)
		return dev_err_probe(dev, ret, "getting firmware version\n");

	dev_dbg(dev, "Firmware version: %.*s\n", resp[RESP_LEN] - 3, resp + RESP_DATA);

	/* Initialize bl_power to a known value */
	ret = dell_uart_set_bl_power(dell_bl, FB_BLANK_UNBLANK);
	if (ret)
		return ret;

	ret = dell_uart_get_brightness(dell_bl);
	if (ret < 0)
		return ret;

	props.type = BACKLIGHT_PLATFORM;
	props.brightness = ret;
	props.max_brightness = DELL_BL_MAX_BRIGHTNESS;
	props.power = dell_bl->power;

	dell_bl->bl = devm_backlight_device_register(dev, "dell_uart_backlight",
						     dev, dell_bl,
						     &dell_uart_backlight_ops,
						     &props);
	return PTR_ERR_OR_ZERO(dell_bl->bl);
}

struct serdev_device_driver dell_uart_bl_serdev_driver = {
	.probe = dell_uart_bl_serdev_probe,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};

static int dell_uart_bl_pdev_probe(struct platform_device *pdev)
{
	enum acpi_backlight_type bl_type;
	struct serdev_device *serdev;
	struct device *ctrl_dev;
	int ret;

	bl_type = acpi_video_get_backlight_type();
	if (bl_type != acpi_backlight_dell_uart) {
		dev_dbg(&pdev->dev, "Not loading (ACPI backlight type = %d)\n", bl_type);
		return -ENODEV;
	}

	ctrl_dev = get_serdev_controller("DELL0501", NULL, 0, "serial0");
	if (IS_ERR(ctrl_dev))
		return PTR_ERR(ctrl_dev);

	serdev = serdev_device_alloc(to_serdev_controller(ctrl_dev));
	put_device(ctrl_dev);
	if (!serdev)
		return -ENOMEM;

	ret = serdev_device_add(serdev);
	if (ret) {
		dev_err(&pdev->dev, "error %d adding serdev\n", ret);
		serdev_device_put(serdev);
		return ret;
	}

	ret = serdev_device_driver_register(&dell_uart_bl_serdev_driver);
	if (ret)
		goto err_remove_serdev;

	/*
	 * serdev device <-> driver matching relies on OF or ACPI matches and
	 * neither is available here, manually bind the driver.
	 */
	ret = device_driver_attach(&dell_uart_bl_serdev_driver.driver, &serdev->dev);
	if (ret)
		goto err_unregister_serdev_driver;

	/* So that dell_uart_bl_pdev_remove() can remove the serdev */
	platform_set_drvdata(pdev, serdev);
	return 0;

err_unregister_serdev_driver:
	serdev_device_driver_unregister(&dell_uart_bl_serdev_driver);
err_remove_serdev:
	serdev_device_remove(serdev);
	return ret;
}

static void dell_uart_bl_pdev_remove(struct platform_device *pdev)
{
	struct serdev_device *serdev = platform_get_drvdata(pdev);

	serdev_device_driver_unregister(&dell_uart_bl_serdev_driver);
	serdev_device_remove(serdev);
}

static struct platform_driver dell_uart_bl_pdev_driver = {
	.probe = dell_uart_bl_pdev_probe,
	.remove = dell_uart_bl_pdev_remove,
	.driver = {
		.name = "dell-uart-backlight",
	},
};
module_platform_driver(dell_uart_bl_pdev_driver);

MODULE_ALIAS("platform:dell-uart-backlight");
MODULE_DESCRIPTION("Dell AIO Serial Backlight driver");
MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_LICENSE("GPL");
