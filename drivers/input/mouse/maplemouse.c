/*
 *	$Id: maplemouse.c,v 1.2 2004/03/22 01:18:15 lethal Exp $
 *	SEGA Dreamcast mouse driver
 *	Based on drivers/usb/usbmouse.c
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/maple.h>

MODULE_AUTHOR("YAEGASHI Takeshi <t@keshi.org>");
MODULE_DESCRIPTION("SEGA Dreamcast mouse driver");

static void dc_mouse_callback(struct mapleq *mq)
{
	int buttons, relx, rely, relz;
	struct maple_device *mapledev = mq->dev;
	struct input_dev *dev = mapledev->private_data;
	unsigned char *res = mq->recvbuf;

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

static int dc_mouse_connect(struct maple_device *dev)
{
	unsigned long data = be32_to_cpu(dev->devinfo.function_data[0]);
	struct input_dev *input_dev;

	dev->private_data = input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	dev->private_data = input_dev;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	input_dev->keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_MIDDLE);
	input_dev->relbit[0] = BIT(REL_X) | BIT(REL_Y) | BIT(REL_WHEEL);

	input_dev->name = dev->product_name;
	input_dev->id.bustype = BUS_MAPLE;

	input_register_device(input_dev);

	maple_getcond_callback(dev, dc_mouse_callback, 1, MAPLE_FUNC_MOUSE);

	return 0;
}


static void dc_mouse_disconnect(struct maple_device *dev)
{
	struct input_dev *input_dev = dev->private_data;

	input_unregister_device(input_dev);
}


static struct maple_driver dc_mouse_driver = {
	.function =	MAPLE_FUNC_MOUSE,
	.name =		"Dreamcast mouse",
	.connect =	dc_mouse_connect,
	.disconnect =	dc_mouse_disconnect,
};


static int __init dc_mouse_init(void)
{
	maple_register_driver(&dc_mouse_driver);
	return 0;
}


static void __exit dc_mouse_exit(void)
{
	maple_unregister_driver(&dc_mouse_driver);
}


module_init(dc_mouse_init);
module_exit(dc_mouse_exit);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
