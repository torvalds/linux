// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PlayStation 1/2 joypads via SPI interface Driver
 *
 * Copyright (C) 2017 Tomohiro Yoshidomi <sylph23k@gmail.com>
 *
 * PlayStation 1/2 joypad's plug (not socket)
 *  123 456 789
 * (...|...|...)
 *
 * 1: DAT -> MISO (pullup with 1k owm to 3.3V)
 * 2: CMD -> MOSI
 * 3: 9V (for motor, if not use N.C.)
 * 4: GND
 * 5: 3.3V
 * 6: Attention -> CS(SS)
 * 7: SCK -> SCK
 * 8: N.C.
 * 9: ACK -> N.C.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#define REVERSE_BIT(x) ((((x) & 0x80) >> 7) | (((x) & 0x40) >> 5) | \
	(((x) & 0x20) >> 3) | (((x) & 0x10) >> 1) | (((x) & 0x08) << 1) | \
	(((x) & 0x04) << 3) | (((x) & 0x02) << 5) | (((x) & 0x01) << 7))

/* PlayStation 1/2 joypad command and response are LSBFIRST. */

/*
 *	0x01, 0x42, 0x00, 0x00, 0x00,
 *	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 *	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
 */
static const u8 PSX_CMD_POLL[] = {
	0x80, 0x42, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
/*	0x01, 0x43, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 */
static const u8 PSX_CMD_ENTER_CFG[] = {
	0x80, 0xC2, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00
};
/*	0x01, 0x43, 0x00, 0x00, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A */
static const u8 PSX_CMD_EXIT_CFG[] = {
	0x80, 0xC2, 0x00, 0x00, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A
};
/*	0x01, 0x4D, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF */
static const u8 PSX_CMD_ENABLE_MOTOR[]	= {
	0x80, 0xB2, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF
};

struct psxpad {
	struct spi_device *spi;
	struct input_dev *idev;
	char phys[0x20];
	bool motor1enable;
	bool motor2enable;
	u8 motor1level;
	u8 motor2level;
	u8 sendbuf[0x20] ____cacheline_aligned;
	u8 response[sizeof(PSX_CMD_POLL)] ____cacheline_aligned;
};

static int psxpad_command(struct psxpad *pad, const u8 sendcmdlen)
{
	struct spi_transfer xfers = {
		.tx_buf		= pad->sendbuf,
		.rx_buf		= pad->response,
		.len		= sendcmdlen,
	};
	int err;

	err = spi_sync_transfer(pad->spi, &xfers, 1);
	if (err) {
		dev_err(&pad->spi->dev,
			"%s: failed to SPI xfers mode: %d\n",
			__func__, err);
		return err;
	}

	return 0;
}

#ifdef CONFIG_JOYSTICK_PSXPAD_SPI_FF
static void psxpad_control_motor(struct psxpad *pad,
				 bool motor1enable, bool motor2enable)
{
	int err;

	pad->motor1enable = motor1enable;
	pad->motor2enable = motor2enable;

	memcpy(pad->sendbuf, PSX_CMD_ENTER_CFG, sizeof(PSX_CMD_ENTER_CFG));
	err = psxpad_command(pad, sizeof(PSX_CMD_ENTER_CFG));
	if (err) {
		dev_err(&pad->spi->dev,
			"%s: failed to enter config mode: %d\n",
			__func__, err);
		return;
	}

	memcpy(pad->sendbuf, PSX_CMD_ENABLE_MOTOR,
	       sizeof(PSX_CMD_ENABLE_MOTOR));
	pad->sendbuf[3] = pad->motor1enable ? 0x00 : 0xFF;
	pad->sendbuf[4] = pad->motor2enable ? 0x80 : 0xFF;
	err = psxpad_command(pad, sizeof(PSX_CMD_ENABLE_MOTOR));
	if (err) {
		dev_err(&pad->spi->dev,
			"%s: failed to enable motor mode: %d\n",
			__func__, err);
		return;
	}

	memcpy(pad->sendbuf, PSX_CMD_EXIT_CFG, sizeof(PSX_CMD_EXIT_CFG));
	err = psxpad_command(pad, sizeof(PSX_CMD_EXIT_CFG));
	if (err) {
		dev_err(&pad->spi->dev,
			"%s: failed to exit config mode: %d\n",
			__func__, err);
		return;
	}
}

static void psxpad_set_motor_level(struct psxpad *pad,
				   u8 motor1level, u8 motor2level)
{
	pad->motor1level = motor1level ? 0xFF : 0x00;
	pad->motor2level = REVERSE_BIT(motor2level);
}

static int psxpad_spi_play_effect(struct input_dev *idev,
				  void *data, struct ff_effect *effect)
{
	struct psxpad *pad = input_get_drvdata(idev);

	switch (effect->type) {
	case FF_RUMBLE:
		psxpad_set_motor_level(pad,
			(effect->u.rumble.weak_magnitude >> 8) & 0xFFU,
			(effect->u.rumble.strong_magnitude >> 8) & 0xFFU);
		break;
	}

	return 0;
}

static int psxpad_spi_init_ff(struct psxpad *pad)
{
	int err;

	input_set_capability(pad->idev, EV_FF, FF_RUMBLE);

	err = input_ff_create_memless(pad->idev, NULL, psxpad_spi_play_effect);
	if (err) {
		dev_err(&pad->spi->dev,
			"input_ff_create_memless() failed: %d\n", err);
		return err;
	}

	return 0;
}

#else	/* CONFIG_JOYSTICK_PSXPAD_SPI_FF */

static void psxpad_control_motor(struct psxpad *pad,
				 bool motor1enable, bool motor2enable)
{
}

static void psxpad_set_motor_level(struct psxpad *pad,
				   u8 motor1level, u8 motor2level)
{
}

static inline int psxpad_spi_init_ff(struct psxpad *pad)
{
	return 0;
}
#endif	/* CONFIG_JOYSTICK_PSXPAD_SPI_FF */

static int psxpad_spi_poll_open(struct input_dev *input)
{
	struct psxpad *pad = input_get_drvdata(input);

	pm_runtime_get_sync(&pad->spi->dev);

	return 0;
}

static void psxpad_spi_poll_close(struct input_dev *input)
{
	struct psxpad *pad = input_get_drvdata(input);

	pm_runtime_put_sync(&pad->spi->dev);
}

static void psxpad_spi_poll(struct input_dev *input)
{
	struct psxpad *pad = input_get_drvdata(input);
	u8 b_rsp3, b_rsp4;
	int err;

	psxpad_control_motor(pad, true, true);

	memcpy(pad->sendbuf, PSX_CMD_POLL, sizeof(PSX_CMD_POLL));
	pad->sendbuf[3] = pad->motor1enable ? pad->motor1level : 0x00;
	pad->sendbuf[4] = pad->motor2enable ? pad->motor2level : 0x00;
	err = psxpad_command(pad, sizeof(PSX_CMD_POLL));
	if (err) {
		dev_err(&pad->spi->dev,
			"%s: poll command failed mode: %d\n", __func__, err);
		return;
	}

	switch (pad->response[1]) {
	case 0xCE:	/* 0x73 : analog 1 */
		/* button data is inverted */
		b_rsp3 = ~pad->response[3];
		b_rsp4 = ~pad->response[4];

		input_report_abs(input, ABS_X, REVERSE_BIT(pad->response[7]));
		input_report_abs(input, ABS_Y, REVERSE_BIT(pad->response[8]));
		input_report_abs(input, ABS_RX, REVERSE_BIT(pad->response[5]));
		input_report_abs(input, ABS_RY, REVERSE_BIT(pad->response[6]));
		input_report_key(input, BTN_DPAD_UP, b_rsp3 & BIT(3));
		input_report_key(input, BTN_DPAD_DOWN, b_rsp3 & BIT(1));
		input_report_key(input, BTN_DPAD_LEFT, b_rsp3 & BIT(0));
		input_report_key(input, BTN_DPAD_RIGHT, b_rsp3 & BIT(2));
		input_report_key(input, BTN_X, b_rsp4 & BIT(3));
		input_report_key(input, BTN_A, b_rsp4 & BIT(2));
		input_report_key(input, BTN_B, b_rsp4 & BIT(1));
		input_report_key(input, BTN_Y, b_rsp4 & BIT(0));
		input_report_key(input, BTN_TL, b_rsp4 & BIT(5));
		input_report_key(input, BTN_TR, b_rsp4 & BIT(4));
		input_report_key(input, BTN_TL2, b_rsp4 & BIT(7));
		input_report_key(input, BTN_TR2, b_rsp4 & BIT(6));
		input_report_key(input, BTN_THUMBL, b_rsp3 & BIT(6));
		input_report_key(input, BTN_THUMBR, b_rsp3 & BIT(5));
		input_report_key(input, BTN_SELECT, b_rsp3 & BIT(7));
		input_report_key(input, BTN_START, b_rsp3 & BIT(4));
		break;

	case 0x82:	/* 0x41 : digital */
		/* button data is inverted */
		b_rsp3 = ~pad->response[3];
		b_rsp4 = ~pad->response[4];

		input_report_abs(input, ABS_X, 0x80);
		input_report_abs(input, ABS_Y, 0x80);
		input_report_abs(input, ABS_RX, 0x80);
		input_report_abs(input, ABS_RY, 0x80);
		input_report_key(input, BTN_DPAD_UP, b_rsp3 & BIT(3));
		input_report_key(input, BTN_DPAD_DOWN, b_rsp3 & BIT(1));
		input_report_key(input, BTN_DPAD_LEFT, b_rsp3 & BIT(0));
		input_report_key(input, BTN_DPAD_RIGHT, b_rsp3 & BIT(2));
		input_report_key(input, BTN_X, b_rsp4 & BIT(3));
		input_report_key(input, BTN_A, b_rsp4 & BIT(2));
		input_report_key(input, BTN_B, b_rsp4 & BIT(1));
		input_report_key(input, BTN_Y, b_rsp4 & BIT(0));
		input_report_key(input, BTN_TL, b_rsp4 & BIT(5));
		input_report_key(input, BTN_TR, b_rsp4 & BIT(4));
		input_report_key(input, BTN_TL2, b_rsp4 & BIT(7));
		input_report_key(input, BTN_TR2, b_rsp4 & BIT(6));
		input_report_key(input, BTN_THUMBL, false);
		input_report_key(input, BTN_THUMBR, false);
		input_report_key(input, BTN_SELECT, b_rsp3 & BIT(7));
		input_report_key(input, BTN_START, b_rsp3 & BIT(4));
		break;
	}

	input_sync(input);
}

static int psxpad_spi_probe(struct spi_device *spi)
{
	struct psxpad *pad;
	struct input_dev *idev;
	int err;

	pad = devm_kzalloc(&spi->dev, sizeof(struct psxpad), GFP_KERNEL);
	if (!pad)
		return -ENOMEM;

	idev = devm_input_allocate_device(&spi->dev);
	if (!idev) {
		dev_err(&spi->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	/* input poll device settings */
	pad->idev = idev;
	pad->spi = spi;

	/* input device settings */
	input_set_drvdata(idev, pad);

	idev->name = "PlayStation 1/2 joypad";
	snprintf(pad->phys, sizeof(pad->phys), "%s/input", dev_name(&spi->dev));
	idev->id.bustype = BUS_SPI;

	idev->open = psxpad_spi_poll_open;
	idev->close = psxpad_spi_poll_close;

	/* key/value map settings */
	input_set_abs_params(idev, ABS_X, 0, 255, 0, 0);
	input_set_abs_params(idev, ABS_Y, 0, 255, 0, 0);
	input_set_abs_params(idev, ABS_RX, 0, 255, 0, 0);
	input_set_abs_params(idev, ABS_RY, 0, 255, 0, 0);
	input_set_capability(idev, EV_KEY, BTN_DPAD_UP);
	input_set_capability(idev, EV_KEY, BTN_DPAD_DOWN);
	input_set_capability(idev, EV_KEY, BTN_DPAD_LEFT);
	input_set_capability(idev, EV_KEY, BTN_DPAD_RIGHT);
	input_set_capability(idev, EV_KEY, BTN_A);
	input_set_capability(idev, EV_KEY, BTN_B);
	input_set_capability(idev, EV_KEY, BTN_X);
	input_set_capability(idev, EV_KEY, BTN_Y);
	input_set_capability(idev, EV_KEY, BTN_TL);
	input_set_capability(idev, EV_KEY, BTN_TR);
	input_set_capability(idev, EV_KEY, BTN_TL2);
	input_set_capability(idev, EV_KEY, BTN_TR2);
	input_set_capability(idev, EV_KEY, BTN_THUMBL);
	input_set_capability(idev, EV_KEY, BTN_THUMBR);
	input_set_capability(idev, EV_KEY, BTN_SELECT);
	input_set_capability(idev, EV_KEY, BTN_START);

	err = psxpad_spi_init_ff(pad);
	if (err)
		return err;

	/* SPI settings */
	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	/* (PlayStation 1/2 joypad might be possible works 250kHz/500kHz) */
	spi->master->min_speed_hz = 125000;
	spi->master->max_speed_hz = 125000;
	spi_setup(spi);

	/* pad settings */
	psxpad_set_motor_level(pad, 0, 0);


	err = input_setup_polling(idev, psxpad_spi_poll);
	if (err) {
		dev_err(&spi->dev, "failed to set up polling: %d\n", err);
		return err;
	}

	/* poll interval is about 60fps */
	input_set_poll_interval(idev, 16);
	input_set_min_poll_interval(idev, 8);
	input_set_max_poll_interval(idev, 32);

	/* register input poll device */
	err = input_register_device(idev);
	if (err) {
		dev_err(&spi->dev,
			"failed to register input device: %d\n", err);
		return err;
	}

	pm_runtime_enable(&spi->dev);

	return 0;
}

static int __maybe_unused psxpad_spi_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct psxpad *pad = spi_get_drvdata(spi);

	psxpad_set_motor_level(pad, 0, 0);

	return 0;
}

static SIMPLE_DEV_PM_OPS(psxpad_spi_pm, psxpad_spi_suspend, NULL);

static const struct spi_device_id psxpad_spi_id[] = {
	{ "psxpad-spi", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, psxpad_spi_id);

static struct spi_driver psxpad_spi_driver = {
	.driver = {
		.name = "psxpad-spi",
		.pm = &psxpad_spi_pm,
	},
	.id_table = psxpad_spi_id,
	.probe   = psxpad_spi_probe,
};

module_spi_driver(psxpad_spi_driver);

MODULE_AUTHOR("Tomohiro Yoshidomi <sylph23k@gmail.com>");
MODULE_DESCRIPTION("PlayStation 1/2 joypads via SPI interface Driver");
MODULE_LICENSE("GPL");
