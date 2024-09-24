// SPDX-License-Identifier: GPL-2.0-only
/*
 *	SEGA Dreamcast controller driver
 *	Based on drivers/usb/iforce.c
 *
 *	Copyright Yaegashi Takeshi, 2001
 *	Adrian McMenamin, 2008 - 2009
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/maple.h>

MODULE_AUTHOR("Adrian McMenamin <adrian@mcmen.demon.co.uk>");
MODULE_DESCRIPTION("SEGA Dreamcast controller driver");
MODULE_LICENSE("GPL");

struct dc_pad {
	struct input_dev *dev;
	struct maple_device *mdev;
};

static void dc_pad_callback(struct mapleq *mq)
{
	unsigned short buttons;
	struct maple_device *mapledev = mq->dev;
	struct dc_pad *pad = maple_get_drvdata(mapledev);
	struct input_dev *dev = pad->dev;
	unsigned char *res = mq->recvbuf->buf;

	buttons = ~le16_to_cpup((__le16 *)(res + 8));

	input_report_abs(dev, ABS_HAT0Y,
		(buttons & 0x0010 ? -1 : 0) + (buttons & 0x0020 ? 1 : 0));
	input_report_abs(dev, ABS_HAT0X,
		(buttons & 0x0040 ? -1 : 0) + (buttons & 0x0080 ? 1 : 0));
	input_report_abs(dev, ABS_HAT1Y,
		(buttons & 0x1000 ? -1 : 0) + (buttons & 0x2000 ? 1 : 0));
	input_report_abs(dev, ABS_HAT1X,
		(buttons & 0x4000 ? -1 : 0) + (buttons & 0x8000 ? 1 : 0));

	input_report_key(dev, BTN_C,      buttons & 0x0001);
	input_report_key(dev, BTN_B,      buttons & 0x0002);
	input_report_key(dev, BTN_A,      buttons & 0x0004);
	input_report_key(dev, BTN_START,  buttons & 0x0008);
	input_report_key(dev, BTN_Z,      buttons & 0x0100);
	input_report_key(dev, BTN_Y,      buttons & 0x0200);
	input_report_key(dev, BTN_X,      buttons & 0x0400);
	input_report_key(dev, BTN_SELECT, buttons & 0x0800);

	input_report_abs(dev, ABS_GAS,    res[10]);
	input_report_abs(dev, ABS_BRAKE,  res[11]);
	input_report_abs(dev, ABS_X,      res[12]);
	input_report_abs(dev, ABS_Y,      res[13]);
	input_report_abs(dev, ABS_RX,     res[14]);
	input_report_abs(dev, ABS_RY,     res[15]);
}

static int dc_pad_open(struct input_dev *dev)
{
	struct dc_pad *pad = dev_get_platdata(&dev->dev);

	maple_getcond_callback(pad->mdev, dc_pad_callback, HZ/20,
		MAPLE_FUNC_CONTROLLER);

	return 0;
}

static void dc_pad_close(struct input_dev *dev)
{
	struct dc_pad *pad = dev_get_platdata(&dev->dev);

	maple_getcond_callback(pad->mdev, dc_pad_callback, 0,
		MAPLE_FUNC_CONTROLLER);
}

/* allow the controller to be used */
static int probe_maple_controller(struct device *dev)
{
	static const short btn_bit[32] = {
		BTN_C, BTN_B, BTN_A, BTN_START, -1, -1, -1, -1,
		BTN_Z, BTN_Y, BTN_X, BTN_SELECT, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
	};

	static const short abs_bit[32] = {
		-1, -1, -1, -1, ABS_HAT0Y, ABS_HAT0Y, ABS_HAT0X, ABS_HAT0X,
		-1, -1, -1, -1, ABS_HAT1Y, ABS_HAT1Y, ABS_HAT1X, ABS_HAT1X,
		ABS_GAS, ABS_BRAKE, ABS_X, ABS_Y, ABS_RX, ABS_RY, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
	};

	struct maple_device *mdev = to_maple_dev(dev);
	struct maple_driver *mdrv = to_maple_driver(dev->driver);
	int i, error;
	struct dc_pad *pad;
	struct input_dev *idev;
	unsigned long data = be32_to_cpu(mdev->devinfo.function_data[0]);

	pad = kzalloc(sizeof(*pad), GFP_KERNEL);
	idev = input_allocate_device();
	if (!pad || !idev) {
		error = -ENOMEM;
		goto fail;
	}

	pad->dev = idev;
	pad->mdev = mdev;

	idev->open = dc_pad_open;
	idev->close = dc_pad_close;

	for (i = 0; i < 32; i++) {
		if (data & (1 << i)) {
			if (btn_bit[i] >= 0)
				__set_bit(btn_bit[i], idev->keybit);
			else if (abs_bit[i] >= 0)
				__set_bit(abs_bit[i], idev->absbit);
		}
	}

	if (idev->keybit[BIT_WORD(BTN_JOYSTICK)])
		idev->evbit[0] |= BIT_MASK(EV_KEY);

	if (idev->absbit[0])
		idev->evbit[0] |= BIT_MASK(EV_ABS);

	for (i = ABS_X; i <= ABS_BRAKE; i++)
		input_set_abs_params(idev, i, 0, 255, 0, 0);

	for (i = ABS_HAT0X; i <= ABS_HAT3Y; i++)
		input_set_abs_params(idev, i, 1, -1, 0, 0);

	idev->dev.platform_data = pad;
	idev->dev.parent = &mdev->dev;
	idev->name = mdev->product_name;
	idev->id.bustype = BUS_HOST;

	error = input_register_device(idev);
	if (error)
		goto fail;

	mdev->driver = mdrv;
	maple_set_drvdata(mdev, pad);

	return 0;

fail:
	input_free_device(idev);
	kfree(pad);
	maple_set_drvdata(mdev, NULL);
	return error;
}

static int remove_maple_controller(struct device *dev)
{
	struct maple_device *mdev = to_maple_dev(dev);
	struct dc_pad *pad = maple_get_drvdata(mdev);

	mdev->callback = NULL;
	input_unregister_device(pad->dev);
	maple_set_drvdata(mdev, NULL);
	kfree(pad);

	return 0;
}

static struct maple_driver dc_pad_driver = {
	.function =	MAPLE_FUNC_CONTROLLER,
	.drv = {
		.name	= "Dreamcast_controller",
		.probe	= probe_maple_controller,
		.remove	= remove_maple_controller,
	},
};

static int __init dc_pad_init(void)
{
	return maple_driver_register(&dc_pad_driver);
}

static void __exit dc_pad_exit(void)
{
	maple_driver_unregister(&dc_pad_driver);
}

module_init(dc_pad_init);
module_exit(dc_pad_exit);
