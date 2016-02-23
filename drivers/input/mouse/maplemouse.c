/*
 *	SEGA Dreamcast mouse driver
 *	Based on drivers/usb/usbmouse.c
 *
 *	Copyright (c) Yaegashi Takeshi, 2001
 *	Copyright (c) Adrian McMenamin, 2008 - 2009
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/maple.h>

MODULE_AUTHOR("Adrian McMenamin <adrian@mcmen.demon.co.uk>");
MODULE_DESCRIPTION("SEGA Dreamcast mouse driver");
MODULE_LICENSE("GPL");

struct dc_mouse {
	struct input_dev *dev;
	struct maple_device *mdev;
};

static void dc_mouse_callback(struct mapleq *mq)
{
	int buttons, relx, rely, relz;
	struct maple_device *mapledev = mq->dev;
	struct dc_mouse *mse = maple_get_drvdata(mapledev);
	struct input_dev *dev = mse->dev;
	unsigned char *res = mq->recvbuf->buf;

	buttons = ~res[8];
	relx = *(unsigned short *)(res + 12) - 512;
	rely = *(unsigned short *)(res + 14) - 512;
	relz = *(unsigned short *)(res + 16) - 512;

	input_report_key(dev, BTN_LEFT,   buttons & 4);
	input_report_key(dev, BTN_MIDDLE, buttons & 9);
	input_report_key(dev, BTN_RIGHT,  buttons & 2);
	input_report_rel(dev, REL_X,      relx);
	input_report_rel(dev, REL_Y,      rely);
	input_report_rel(dev, REL_WHEEL,  relz);
	input_sync(dev);
}

static int dc_mouse_open(struct input_dev *dev)
{
	struct dc_mouse *mse = maple_get_drvdata(to_maple_dev(&dev->dev));

	maple_getcond_callback(mse->mdev, dc_mouse_callback, HZ/50,
		MAPLE_FUNC_MOUSE);

	return 0;
}

static void dc_mouse_close(struct input_dev *dev)
{
	struct dc_mouse *mse = maple_get_drvdata(to_maple_dev(&dev->dev));

	maple_getcond_callback(mse->mdev, dc_mouse_callback, 0,
		MAPLE_FUNC_MOUSE);
}

/* allow the mouse to be used */
static int probe_maple_mouse(struct device *dev)
{
	struct maple_device *mdev = to_maple_dev(dev);
	struct maple_driver *mdrv = to_maple_driver(dev->driver);
	int error;
	struct input_dev *input_dev;
	struct dc_mouse *mse;

	mse = kzalloc(sizeof(struct dc_mouse), GFP_KERNEL);
	if (!mse) {
		error = -ENOMEM;
		goto fail;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		error = -ENOMEM;
		goto fail_nomem;
	}

	mse->dev = input_dev;
	mse->mdev = mdev;

	input_set_drvdata(input_dev, mse);
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y) |
		BIT_MASK(REL_WHEEL);
	input_dev->open = dc_mouse_open;
	input_dev->close = dc_mouse_close;
	input_dev->name = mdev->product_name;
	input_dev->id.bustype = BUS_HOST;
	error =	input_register_device(input_dev);
	if (error)
		goto fail_register;

	mdev->driver = mdrv;
	maple_set_drvdata(mdev, mse);

	return error;

fail_register:
	input_free_device(input_dev);
fail_nomem:
	kfree(mse);
fail:
	return error;
}

static int remove_maple_mouse(struct device *dev)
{
	struct maple_device *mdev = to_maple_dev(dev);
	struct dc_mouse *mse = maple_get_drvdata(mdev);

	mdev->callback = NULL;
	input_unregister_device(mse->dev);
	maple_set_drvdata(mdev, NULL);
	kfree(mse);

	return 0;
}

static struct maple_driver dc_mouse_driver = {
	.function =	MAPLE_FUNC_MOUSE,
	.drv = {
		.name = "Dreamcast_mouse",
		.probe = probe_maple_mouse,
		.remove = remove_maple_mouse,
	},
};

static int __init dc_mouse_init(void)
{
	return maple_driver_register(&dc_mouse_driver);
}

static void __exit dc_mouse_exit(void)
{
	maple_driver_unregister(&dc_mouse_driver);
}

module_init(dc_mouse_init);
module_exit(dc_mouse_exit);
