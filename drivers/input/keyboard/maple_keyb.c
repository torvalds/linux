/*
 *	$Id: maple_keyb.c,v 1.4 2004/03/22 01:18:15 lethal Exp $
 * 	SEGA Dreamcast keyboard driver
 *	Based on drivers/usb/usbkbd.c
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/maple.h>

MODULE_AUTHOR("YAEGASHI Takeshi <t@keshi.org>");
MODULE_DESCRIPTION("SEGA Dreamcast keyboard driver");
MODULE_LICENSE("GPL");

static unsigned char dc_kbd_keycode[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117,183,184,185,186,187,188,189,190,
	191,192,193,194,134,138,130,132,128,129,131,137,133,135,136,113,
	115,114,  0,  0,  0,121,  0, 89, 93,124, 92, 94, 95,  0,  0,  0,
	122,123, 90, 91, 85,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
	150,158,159,128,136,177,178,176,142,152,173,140
};


struct dc_kbd {
	struct input_dev dev;
	unsigned char new[8];
	unsigned char old[8];
	int open;
};


static void dc_scan_kbd(struct dc_kbd *kbd)
{
	int i;
	struct input_dev *dev = &kbd->dev;

	for(i=0; i<8; i++)
		input_report_key(dev,
				 dc_kbd_keycode[i+224],
				 (kbd->new[0]>>i)&1);

	for(i=2; i<8; i++) {

		if(kbd->old[i]>3&&memscan(kbd->new+2, kbd->old[i], 6)==NULL) {
			if(dc_kbd_keycode[kbd->old[i]])
				input_report_key(dev,
						 dc_kbd_keycode[kbd->old[i]],
						 0);
			else
				printk("Unknown key (scancode %#x) released.",
				       kbd->old[i]);
		}

		if(kbd->new[i]>3&&memscan(kbd->old+2, kbd->new[i], 6)!=NULL) {
			if(dc_kbd_keycode[kbd->new[i]])
				input_report_key(dev,
						 dc_kbd_keycode[kbd->new[i]],
						 1);
			else
				printk("Unknown key (scancode %#x) pressed.",
				       kbd->new[i]);
		}
	}

	input_sync(dev);

	memcpy(kbd->old, kbd->new, 8);
}


static void dc_kbd_callback(struct mapleq *mq)
{
	struct maple_device *mapledev = mq->dev;
	struct dc_kbd *kbd = mapledev->private_data;
	unsigned long *buf = mq->recvbuf;

	if (buf[1] == mapledev->function) {
		memcpy(kbd->new, buf+2, 8);
		dc_scan_kbd(kbd);
	}
}


static int dc_kbd_open(struct input_dev *dev)
{
	struct dc_kbd *kbd = dev->private;
	kbd->open++;
	return 0;
}


static void dc_kbd_close(struct input_dev *dev)
{
	struct dc_kbd *kbd = dev->private;
	kbd->open--;
}


static int dc_kbd_connect(struct maple_device *dev)
{
	int i;
	unsigned long data = be32_to_cpu(dev->devinfo.function_data[0]);
	struct dc_kbd *kbd;

	if (!(kbd = kmalloc(sizeof(struct dc_kbd), GFP_KERNEL)))
		return -1;
	memset(kbd, 0, sizeof(struct dc_kbd));

	dev->private_data = kbd;

	kbd->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REP);

	init_input_dev(&kbd->dev);

	for (i=0; i<255; i++)
		set_bit(dc_kbd_keycode[i], kbd->dev.keybit);

	clear_bit(0, kbd->dev.keybit);

	kbd->dev.private = kbd;
	kbd->dev.open = dc_kbd_open;
	kbd->dev.close = dc_kbd_close;
	kbd->dev.event = NULL;

	kbd->dev.name = dev->product_name;
	kbd->dev.id.bustype = BUS_MAPLE;

	input_register_device(&kbd->dev);

	maple_getcond_callback(dev, dc_kbd_callback, 1, MAPLE_FUNC_KEYBOARD);

	printk(KERN_INFO "input: keyboard(0x%lx): %s\n", data, kbd->dev.name);

	return 0;
}


static void dc_kbd_disconnect(struct maple_device *dev)
{
	struct dc_kbd *kbd = dev->private_data;

	input_unregister_device(&kbd->dev);
	kfree(kbd);
}


static struct maple_driver dc_kbd_driver = {
	.function =	MAPLE_FUNC_KEYBOARD,
	.name =		"Dreamcast keyboard",
	.connect =	dc_kbd_connect,
	.disconnect =	dc_kbd_disconnect,
};


static int __init dc_kbd_init(void)
{
	maple_register_driver(&dc_kbd_driver);
	return 0;
}


static void __exit dc_kbd_exit(void)
{
	maple_unregister_driver(&dc_kbd_driver);
}


module_init(dc_kbd_init);
module_exit(dc_kbd_exit);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
