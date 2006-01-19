/*
 * ati_remote2 - ATI/Philips USB RF remote driver
 *
 * Copyright (C) 2005 Ville Syrjala <syrjala@sci.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/usb_input.h>

#define DRIVER_DESC    "ATI/Philips USB RF remote driver"
#define DRIVER_VERSION "0.1"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR("Ville Syrjala <syrjala@sci.fi>");
MODULE_LICENSE("GPL");

static unsigned int mode_mask = 0x1F;
module_param(mode_mask, uint, 0644);
MODULE_PARM_DESC(mode_mask, "Bitmask of modes to accept <4:PC><3:AUX4><2:AUX3><1:AUX2><0:AUX1>");

static struct usb_device_id ati_remote2_id_table[] = {
	{ USB_DEVICE(0x0471, 0x0602) },	/* ATI Remote Wonder II */
	{ }
};
MODULE_DEVICE_TABLE(usb, ati_remote2_id_table);

static struct {
	int hw_code;
	int key_code;
} ati_remote2_key_table[] = {
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x0c, KEY_POWER },
	{ 0x0d, KEY_MUTE },
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x20, KEY_CHANNELUP },
	{ 0x21, KEY_CHANNELDOWN },
	{ 0x28, KEY_FORWARD },
	{ 0x29, KEY_REWIND },
	{ 0x2c, KEY_PLAY },
	{ 0x30, KEY_PAUSE },
	{ 0x31, KEY_STOP },
	{ 0x37, KEY_RECORD },
	{ 0x38, KEY_DVD },
	{ 0x39, KEY_TV },
	{ 0x54, KEY_MENU },
	{ 0x58, KEY_UP },
	{ 0x59, KEY_DOWN },
	{ 0x5a, KEY_LEFT },
	{ 0x5b, KEY_RIGHT },
	{ 0x5c, KEY_OK },
	{ 0x78, KEY_A },
	{ 0x79, KEY_B },
	{ 0x7a, KEY_C },
	{ 0x7b, KEY_D },
	{ 0x7c, KEY_E },
	{ 0x7d, KEY_F },
	{ 0x82, KEY_ENTER },
	{ 0x8e, KEY_VENDOR },
	{ 0x96, KEY_COFFEE },
	{ 0xa9, BTN_LEFT },
	{ 0xaa, BTN_RIGHT },
	{ 0xbe, KEY_QUESTION },
	{ 0xd5, KEY_FRONT },
	{ 0xd0, KEY_EDIT },
	{ 0xf9, KEY_INFO },
	{ (0x00 << 8) | 0x3f, KEY_PROG1 },
	{ (0x01 << 8) | 0x3f, KEY_PROG2 },
	{ (0x02 << 8) | 0x3f, KEY_PROG3 },
	{ (0x03 << 8) | 0x3f, KEY_PROG4 },
	{ (0x04 << 8) | 0x3f, KEY_PC },
	{ 0, KEY_RESERVED }
};

struct ati_remote2 {
	struct input_dev *idev;
	struct usb_device *udev;

	struct usb_interface *intf[2];
	struct usb_endpoint_descriptor *ep[2];
	struct urb *urb[2];
	void *buf[2];
	dma_addr_t buf_dma[2];

	unsigned long jiffies;
	int mode;

	char name[64];
	char phys[64];
};

static int ati_remote2_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void ati_remote2_disconnect(struct usb_interface *interface);

static struct usb_driver ati_remote2_driver = {
	.name       = "ati_remote2",
	.probe      = ati_remote2_probe,
	.disconnect = ati_remote2_disconnect,
	.id_table   = ati_remote2_id_table,
};

static int ati_remote2_open(struct input_dev *idev)
{
	struct ati_remote2 *ar2 = idev->private;
	int r;

	r = usb_submit_urb(ar2->urb[0], GFP_KERNEL);
	if (r) {
		dev_err(&ar2->intf[0]->dev,
			"%s: usb_submit_urb() = %d\n", __FUNCTION__, r);
		return r;
	}
	r = usb_submit_urb(ar2->urb[1], GFP_KERNEL);
	if (r) {
		usb_kill_urb(ar2->urb[0]);
		dev_err(&ar2->intf[1]->dev,
			"%s: usb_submit_urb() = %d\n", __FUNCTION__, r);
		return r;
	}

	return 0;
}

static void ati_remote2_close(struct input_dev *idev)
{
	struct ati_remote2 *ar2 = idev->private;

	usb_kill_urb(ar2->urb[0]);
	usb_kill_urb(ar2->urb[1]);
}

static void ati_remote2_input_mouse(struct ati_remote2 *ar2, struct pt_regs *regs)
{
	struct input_dev *idev = ar2->idev;
	u8 *data = ar2->buf[0];

	if (data[0] > 4) {
		dev_err(&ar2->intf[0]->dev,
			"Unknown mode byte (%02x %02x %02x %02x)\n",
			data[3], data[2], data[1], data[0]);
		return;
	}

	if (!((1 << data[0]) & mode_mask))
		return;

	input_regs(idev, regs);
	input_event(idev, EV_REL, REL_X, (s8) data[1]);
	input_event(idev, EV_REL, REL_Y, (s8) data[2]);
	input_sync(idev);
}

static int ati_remote2_lookup(unsigned int hw_code)
{
	int i;

	for (i = 0; ati_remote2_key_table[i].key_code != KEY_RESERVED; i++)
		if (ati_remote2_key_table[i].hw_code == hw_code)
			return i;

	return -1;
}

static void ati_remote2_input_key(struct ati_remote2 *ar2, struct pt_regs *regs)
{
	struct input_dev *idev = ar2->idev;
	u8 *data = ar2->buf[1];
	int hw_code, index;

	if (data[0] > 4) {
		dev_err(&ar2->intf[1]->dev,
			"Unknown mode byte (%02x %02x %02x %02x)\n",
			data[3], data[2], data[1], data[0]);
		return;
	}

	hw_code = data[2];
	/*
	 * Mode keys (AUX1-AUX4, PC) all generate the same code byte.
	 * Use the mode byte to figure out which one was pressed.
	 */
	if (hw_code == 0x3f) {
		/*
		 * For some incomprehensible reason the mouse pad generates
		 * events which look identical to the events from the last
		 * pressed mode key. Naturally we don't want to generate key
		 * events for the mouse pad so we filter out any subsequent
		 * events from the same mode key.
		 */
		if (ar2->mode == data[0])
			return;

		if (data[1] == 0)
			ar2->mode = data[0];

		hw_code |= data[0] << 8;
	}

	if (!((1 << data[0]) & mode_mask))
		return;

	index = ati_remote2_lookup(hw_code);
	if (index < 0) {
		dev_err(&ar2->intf[1]->dev,
			"Unknown code byte (%02x %02x %02x %02x)\n",
			data[3], data[2], data[1], data[0]);
		return;
	}

	switch (data[1]) {
	case 0:	/* release */
		break;
	case 1:	/* press */
		ar2->jiffies = jiffies + msecs_to_jiffies(idev->rep[REP_DELAY]);
		break;
	case 2:	/* repeat */

		/* No repeat for mouse buttons. */
		if (ati_remote2_key_table[index].key_code == BTN_LEFT ||
		    ati_remote2_key_table[index].key_code == BTN_RIGHT)
			return;

		if (!time_after_eq(jiffies, ar2->jiffies))
			return;

		ar2->jiffies = jiffies + msecs_to_jiffies(idev->rep[REP_PERIOD]);
		break;
	default:
		dev_err(&ar2->intf[1]->dev,
			"Unknown state byte (%02x %02x %02x %02x)\n",
			data[3], data[2], data[1], data[0]);
		return;
	}

	input_regs(idev, regs);
	input_event(idev, EV_KEY, ati_remote2_key_table[index].key_code, data[1]);
	input_sync(idev);
}

static void ati_remote2_complete_mouse(struct urb *urb, struct pt_regs *regs)
{
	struct ati_remote2 *ar2 = urb->context;
	int r;

	switch (urb->status) {
	case 0:
		ati_remote2_input_mouse(ar2, regs);
		break;
	case -ENOENT:
	case -EILSEQ:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev_dbg(&ar2->intf[0]->dev,
			"%s(): urb status = %d\n", __FUNCTION__, urb->status);
		return;
	default:
		dev_err(&ar2->intf[0]->dev,
			"%s(): urb status = %d\n", __FUNCTION__, urb->status);
	}

	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r)
		dev_err(&ar2->intf[0]->dev,
			"%s(): usb_submit_urb() = %d\n", __FUNCTION__, r);
}

static void ati_remote2_complete_key(struct urb *urb, struct pt_regs *regs)
{
	struct ati_remote2 *ar2 = urb->context;
	int r;

	switch (urb->status) {
	case 0:
		ati_remote2_input_key(ar2, regs);
		break;
	case -ENOENT:
	case -EILSEQ:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev_dbg(&ar2->intf[1]->dev,
			"%s(): urb status = %d\n", __FUNCTION__, urb->status);
		return;
	default:
		dev_err(&ar2->intf[1]->dev,
			"%s(): urb status = %d\n", __FUNCTION__, urb->status);
	}

	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r)
		dev_err(&ar2->intf[1]->dev,
			"%s(): usb_submit_urb() = %d\n", __FUNCTION__, r);
}

static int ati_remote2_input_init(struct ati_remote2 *ar2)
{
	struct input_dev *idev;
	int i;

	idev = input_allocate_device();
	if (!idev)
		return -ENOMEM;

	ar2->idev = idev;
	idev->private = ar2;

	idev->evbit[0] = BIT(EV_KEY) | BIT(EV_REP) | BIT(EV_REL);
	idev->keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_RIGHT);
	idev->relbit[0] = BIT(REL_X) | BIT(REL_Y);
	for (i = 0; ati_remote2_key_table[i].key_code != KEY_RESERVED; i++)
		set_bit(ati_remote2_key_table[i].key_code, idev->keybit);

	idev->rep[REP_DELAY]  = 250;
	idev->rep[REP_PERIOD] = 33;

	idev->open = ati_remote2_open;
	idev->close = ati_remote2_close;

	idev->name = ar2->name;
	idev->phys = ar2->phys;

	usb_to_input_id(ar2->udev, &idev->id);
	idev->cdev.dev = &ar2->udev->dev;

	i = input_register_device(idev);
	if (i)
		input_free_device(idev);

	return i;
}

static int ati_remote2_urb_init(struct ati_remote2 *ar2)
{
	struct usb_device *udev = ar2->udev;
	int i, pipe, maxp;

	for (i = 0; i < 2; i++) {
		ar2->buf[i] = usb_buffer_alloc(udev, 4, GFP_KERNEL, &ar2->buf_dma[i]);
		if (!ar2->buf[i])
			return -ENOMEM;

		ar2->urb[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!ar2->urb[i])
			return -ENOMEM;

		pipe = usb_rcvintpipe(udev, ar2->ep[i]->bEndpointAddress);
		maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));
		maxp = maxp > 4 ? 4 : maxp;

		usb_fill_int_urb(ar2->urb[i], udev, pipe, ar2->buf[i], maxp,
				 i ? ati_remote2_complete_key : ati_remote2_complete_mouse,
				 ar2, ar2->ep[i]->bInterval);
		ar2->urb[i]->transfer_dma = ar2->buf_dma[i];
		ar2->urb[i]->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	return 0;
}

static void ati_remote2_urb_cleanup(struct ati_remote2 *ar2)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (ar2->urb[i])
			usb_free_urb(ar2->urb[i]);

		if (ar2->buf[i])
			usb_buffer_free(ar2->udev, 4, ar2->buf[i], ar2->buf_dma[i]);
	}
}

static int ati_remote2_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *alt = interface->cur_altsetting;
	struct ati_remote2 *ar2;
	int r;

	if (alt->desc.bInterfaceNumber)
		return -ENODEV;

	ar2 = kzalloc(sizeof (struct ati_remote2), GFP_KERNEL);
	if (!ar2)
		return -ENOMEM;

	ar2->udev = udev;

	ar2->intf[0] = interface;
	ar2->ep[0] = &alt->endpoint[0].desc;

	ar2->intf[1] = usb_ifnum_to_if(udev, 1);
	r = usb_driver_claim_interface(&ati_remote2_driver, ar2->intf[1], ar2);
	if (r)
		goto fail1;
	alt = ar2->intf[1]->cur_altsetting;
	ar2->ep[1] = &alt->endpoint[0].desc;

	r = ati_remote2_urb_init(ar2);
	if (r)
		goto fail2;

	usb_make_path(udev, ar2->phys, sizeof(ar2->phys));
	strlcat(ar2->phys, "/input0", sizeof(ar2->phys));

	strlcat(ar2->name, "ATI Remote Wonder II", sizeof(ar2->name));

	r = ati_remote2_input_init(ar2);
	if (r)
		goto fail2;

	usb_set_intfdata(interface, ar2);

	return 0;

 fail2:
	ati_remote2_urb_cleanup(ar2);

	usb_driver_release_interface(&ati_remote2_driver, ar2->intf[1]);
 fail1:
	kfree(ar2);

	return r;
}

static void ati_remote2_disconnect(struct usb_interface *interface)
{
	struct ati_remote2 *ar2;
	struct usb_host_interface *alt = interface->cur_altsetting;

	if (alt->desc.bInterfaceNumber)
		return;

	ar2 = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	input_unregister_device(ar2->idev);

	ati_remote2_urb_cleanup(ar2);

	usb_driver_release_interface(&ati_remote2_driver, ar2->intf[1]);

	kfree(ar2);
}

static int __init ati_remote2_init(void)
{
	int r;

	r = usb_register(&ati_remote2_driver);
	if (r)
		printk(KERN_ERR "ati_remote2: usb_register() = %d\n", r);
	else
		printk(KERN_INFO "ati_remote2: " DRIVER_DESC " " DRIVER_VERSION "\n");

	return r;
}

static void __exit ati_remote2_exit(void)
{
	usb_deregister(&ati_remote2_driver);
}

module_init(ati_remote2_init);
module_exit(ati_remote2_exit);
