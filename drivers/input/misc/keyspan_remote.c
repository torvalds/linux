/*
 * keyspan_remote: USB driver for the Keyspan DMR
 *
 * Copyright (C) 2005 Zymeta Corporation - Michael Downey (downey@zymeta.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * This driver has been put together with the support of Innosys, Inc.
 * and Keyspan, Inc the manufacturers of the Keyspan USB DMR product.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb/input.h>

#define DRIVER_VERSION	"v0.1"
#define DRIVER_AUTHOR	"Michael Downey <downey@zymeta.com>"
#define DRIVER_DESC	"Driver for the USB Keyspan remote control."
#define DRIVER_LICENSE	"GPL"

/* Parameters that can be passed to the driver. */
static int debug;
module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "Enable extra debug messages and information");

/* Vendor and product ids */
#define USB_KEYSPAN_VENDOR_ID		0x06CD
#define USB_KEYSPAN_PRODUCT_UIA11	0x0202

/* Defines for converting the data from the remote. */
#define ZERO		0x18
#define ZERO_MASK	0x1F	/* 5 bits for a 0 */
#define ONE		0x3C
#define ONE_MASK	0x3F	/* 6 bits for a 1 */
#define SYNC		0x3F80
#define SYNC_MASK	0x3FFF	/* 14 bits for a SYNC sequence */
#define STOP		0x00
#define STOP_MASK	0x1F	/* 5 bits for the STOP sequence */
#define GAP		0xFF

#define RECV_SIZE	8	/* The UIA-11 type have a 8 byte limit. */

/*
 * Table that maps the 31 possible keycodes to input keys.
 * Currently there are 15 and 17 button models so RESERVED codes
 * are blank areas in the mapping.
 */
static const unsigned short keyspan_key_table[] = {
	KEY_RESERVED,		/* 0 is just a place holder. */
	KEY_RESERVED,
	KEY_STOP,
	KEY_PLAYCD,
	KEY_RESERVED,
	KEY_PREVIOUSSONG,
	KEY_REWIND,
	KEY_FORWARD,
	KEY_NEXTSONG,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_PAUSE,
	KEY_VOLUMEUP,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_VOLUMEDOWN,
	KEY_RESERVED,
	KEY_UP,
	KEY_RESERVED,
	KEY_MUTE,
	KEY_LEFT,
	KEY_ENTER,
	KEY_RIGHT,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_DOWN,
	KEY_RESERVED,
	KEY_KPASTERISK,
	KEY_RESERVED,
	KEY_MENU
};

/* table of devices that work with this driver */
static struct usb_device_id keyspan_table[] = {
	{ USB_DEVICE(USB_KEYSPAN_VENDOR_ID, USB_KEYSPAN_PRODUCT_UIA11) },
	{ }					/* Terminating entry */
};

/* Structure to store all the real stuff that a remote sends to us. */
struct keyspan_message {
	u16	system;
	u8	button;
	u8	toggle;
};

/* Structure used for all the bit testing magic needed to be done. */
struct bit_tester {
	u32	tester;
	int	len;
	int	pos;
	int	bits_left;
	u8	buffer[32];
};

/* Structure to hold all of our driver specific stuff */
struct usb_keyspan {
	char				name[128];
	char				phys[64];
	unsigned short			keymap[ARRAY_SIZE(keyspan_key_table)];
	struct usb_device		*udev;
	struct input_dev		*input;
	struct usb_interface		*interface;
	struct usb_endpoint_descriptor	*in_endpoint;
	struct urb*			irq_urb;
	int				open;
	dma_addr_t			in_dma;
	unsigned char			*in_buffer;

	/* variables used to parse messages from remote. */
	struct bit_tester		data;
	int				stage;
	int				toggle;
};

static struct usb_driver keyspan_driver;

/*
 * Debug routine that prints out what we've received from the remote.
 */
static void keyspan_print(struct usb_keyspan* dev) /*unsigned char* data)*/
{
	char codes[4 * RECV_SIZE];
	int i;

	for (i = 0; i < RECV_SIZE; i++)
		snprintf(codes + i * 3, 4, "%02x ", dev->in_buffer[i]);

	dev_info(&dev->udev->dev, "%s\n", codes);
}

/*
 * Routine that manages the bit_tester structure.  It makes sure that there are
 * at least bits_needed bits loaded into the tester.
 */
static int keyspan_load_tester(struct usb_keyspan* dev, int bits_needed)
{
	if (dev->data.bits_left >= bits_needed)
		return 0;

	/*
	 * Somehow we've missed the last message. The message will be repeated
	 * though so it's not too big a deal
	 */
	if (dev->data.pos >= dev->data.len) {
		dev_dbg(&dev->udev->dev,
			"%s - Error ran out of data. pos: %d, len: %d\n",
			__func__, dev->data.pos, dev->data.len);
		return -1;
	}

	/* Load as much as we can into the tester. */
	while ((dev->data.bits_left + 7 < (sizeof(dev->data.tester) * 8)) &&
	       (dev->data.pos < dev->data.len)) {
		dev->data.tester += (dev->data.buffer[dev->data.pos++] << dev->data.bits_left);
		dev->data.bits_left += 8;
	}

	return 0;
}

static void keyspan_report_button(struct usb_keyspan *remote, int button, int press)
{
	struct input_dev *input = remote->input;

	input_event(input, EV_MSC, MSC_SCAN, button);
	input_report_key(input, remote->keymap[button], press);
	input_sync(input);
}

/*
 * Routine that handles all the logic needed to parse out the message from the remote.
 */
static void keyspan_check_data(struct usb_keyspan *remote)
{
	int i;
	int found = 0;
	struct keyspan_message message;

	switch(remote->stage) {
	case 0:
		/*
		 * In stage 0 we want to find the start of a message.  The remote sends a 0xFF as filler.
		 * So the first byte that isn't a FF should be the start of a new message.
		 */
		for (i = 0; i < RECV_SIZE && remote->in_buffer[i] == GAP; ++i);

		if (i < RECV_SIZE) {
			memcpy(remote->data.buffer, remote->in_buffer, RECV_SIZE);
			remote->data.len = RECV_SIZE;
			remote->data.pos = 0;
			remote->data.tester = 0;
			remote->data.bits_left = 0;
			remote->stage = 1;
		}
		break;

	case 1:
		/*
		 * Stage 1 we should have 16 bytes and should be able to detect a
		 * SYNC.  The SYNC is 14 bits, 7 0's and then 7 1's.
		 */
		memcpy(remote->data.buffer + remote->data.len, remote->in_buffer, RECV_SIZE);
		remote->data.len += RECV_SIZE;

		found = 0;
		while ((remote->data.bits_left >= 14 || remote->data.pos < remote->data.len) && !found) {
			for (i = 0; i < 8; ++i) {
				if (keyspan_load_tester(remote, 14) != 0) {
					remote->stage = 0;
					return;
				}

				if ((remote->data.tester & SYNC_MASK) == SYNC) {
					remote->data.tester = remote->data.tester >> 14;
					remote->data.bits_left -= 14;
					found = 1;
					break;
				} else {
					remote->data.tester = remote->data.tester >> 1;
					--remote->data.bits_left;
				}
			}
		}

		if (!found) {
			remote->stage = 0;
			remote->data.len = 0;
		} else {
			remote->stage = 2;
		}
		break;

	case 2:
		/*
		 * Stage 2 we should have 24 bytes which will be enough for a full
		 * message.  We need to parse out the system code, button code,
		 * toggle code, and stop.
		 */
		memcpy(remote->data.buffer + remote->data.len, remote->in_buffer, RECV_SIZE);
		remote->data.len += RECV_SIZE;

		message.system = 0;
		for (i = 0; i < 9; i++) {
			keyspan_load_tester(remote, 6);

			if ((remote->data.tester & ZERO_MASK) == ZERO) {
				message.system = message.system << 1;
				remote->data.tester = remote->data.tester >> 5;
				remote->data.bits_left -= 5;
			} else if ((remote->data.tester & ONE_MASK) == ONE) {
				message.system = (message.system << 1) + 1;
				remote->data.tester = remote->data.tester >> 6;
				remote->data.bits_left -= 6;
			} else {
				err("%s - Unknown sequence found in system data.\n", __func__);
				remote->stage = 0;
				return;
			}
		}

		message.button = 0;
		for (i = 0; i < 5; i++) {
			keyspan_load_tester(remote, 6);

			if ((remote->data.tester & ZERO_MASK) == ZERO) {
				message.button = message.button << 1;
				remote->data.tester = remote->data.tester >> 5;
				remote->data.bits_left -= 5;
			} else if ((remote->data.tester & ONE_MASK) == ONE) {
				message.button = (message.button << 1) + 1;
				remote->data.tester = remote->data.tester >> 6;
				remote->data.bits_left -= 6;
			} else {
				err("%s - Unknown sequence found in button data.\n", __func__);
				remote->stage = 0;
				return;
			}
		}

		keyspan_load_tester(remote, 6);
		if ((remote->data.tester & ZERO_MASK) == ZERO) {
			message.toggle = 0;
			remote->data.tester = remote->data.tester >> 5;
			remote->data.bits_left -= 5;
		} else if ((remote->data.tester & ONE_MASK) == ONE) {
			message.toggle = 1;
			remote->data.tester = remote->data.tester >> 6;
			remote->data.bits_left -= 6;
		} else {
			err("%s - Error in message, invalid toggle.\n", __func__);
			remote->stage = 0;
			return;
		}

		keyspan_load_tester(remote, 5);
		if ((remote->data.tester & STOP_MASK) == STOP) {
			remote->data.tester = remote->data.tester >> 5;
			remote->data.bits_left -= 5;
		} else {
			err("Bad message recieved, no stop bit found.\n");
		}

		dev_dbg(&remote->udev->dev,
			"%s found valid message: system: %d, button: %d, toggle: %d\n",
			__func__, message.system, message.button, message.toggle);

		if (message.toggle != remote->toggle) {
			keyspan_report_button(remote, message.button, 1);
			keyspan_report_button(remote, message.button, 0);
			remote->toggle = message.toggle;
		}

		remote->stage = 0;
		break;
	}
}

/*
 * Routine for sending all the initialization messages to the remote.
 */
static int keyspan_setup(struct usb_device* dev)
{
	int retval = 0;

	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 0x11, 0x40, 0x5601, 0x0, NULL, 0, 0);
	if (retval) {
		dev_dbg(&dev->dev, "%s - failed to set bit rate due to error: %d\n",
			__func__, retval);
		return(retval);
	}

	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 0x44, 0x40, 0x0, 0x0, NULL, 0, 0);
	if (retval) {
		dev_dbg(&dev->dev, "%s - failed to set resume sensitivity due to error: %d\n",
			__func__, retval);
		return(retval);
	}

	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 0x22, 0x40, 0x0, 0x0, NULL, 0, 0);
	if (retval) {
		dev_dbg(&dev->dev, "%s - failed to turn receive on due to error: %d\n",
			__func__, retval);
		return(retval);
	}

	dev_dbg(&dev->dev, "%s - Setup complete.\n", __func__);
	return(retval);
}

/*
 * Routine used to handle a new message that has come in.
 */
static void keyspan_irq_recv(struct urb *urb)
{
	struct usb_keyspan *dev = urb->context;
	int retval;

	/* Check our status in case we need to bail out early. */
	switch (urb->status) {
	case 0:
		break;

	/* Device went away so don't keep trying to read from it. */
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;

	default:
		goto resubmit;
		break;
	}

	if (debug)
		keyspan_print(dev);

	keyspan_check_data(dev);

resubmit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		err ("%s - usb_submit_urb failed with result: %d", __func__, retval);
}

static int keyspan_open(struct input_dev *dev)
{
	struct usb_keyspan *remote = input_get_drvdata(dev);

	remote->irq_urb->dev = remote->udev;
	if (usb_submit_urb(remote->irq_urb, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void keyspan_close(struct input_dev *dev)
{
	struct usb_keyspan *remote = input_get_drvdata(dev);

	usb_kill_urb(remote->irq_urb);
}

static struct usb_endpoint_descriptor *keyspan_get_in_endpoint(struct usb_host_interface *iface)
{

	struct usb_endpoint_descriptor *endpoint;
	int i;

	for (i = 0; i < iface->desc.bNumEndpoints; ++i) {
		endpoint = &iface->endpoint[i].desc;

		if (usb_endpoint_is_int_in(endpoint)) {
			/* we found our interrupt in endpoint */
			return endpoint;
		}
	}

	return NULL;
}

/*
 * Routine that sets up the driver to handle a specific USB device detected on the bus.
 */
static int keyspan_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_endpoint_descriptor *endpoint;
	struct usb_keyspan *remote;
	struct input_dev *input_dev;
	int i, error;

	endpoint = keyspan_get_in_endpoint(interface->cur_altsetting);
	if (!endpoint)
		return -ENODEV;

	remote = kzalloc(sizeof(*remote), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!remote || !input_dev) {
		error = -ENOMEM;
		goto fail1;
	}

	remote->udev = udev;
	remote->input = input_dev;
	remote->interface = interface;
	remote->in_endpoint = endpoint;
	remote->toggle = -1;	/* Set to -1 so we will always not match the toggle from the first remote message. */

	remote->in_buffer = usb_buffer_alloc(udev, RECV_SIZE, GFP_ATOMIC, &remote->in_dma);
	if (!remote->in_buffer) {
		error = -ENOMEM;
		goto fail1;
	}

	remote->irq_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!remote->irq_urb) {
		error = -ENOMEM;
		goto fail2;
	}

	error = keyspan_setup(udev);
	if (error) {
		error = -ENODEV;
		goto fail3;
	}

	if (udev->manufacturer)
		strlcpy(remote->name, udev->manufacturer, sizeof(remote->name));

	if (udev->product) {
		if (udev->manufacturer)
			strlcat(remote->name, " ", sizeof(remote->name));
		strlcat(remote->name, udev->product, sizeof(remote->name));
	}

	if (!strlen(remote->name))
		snprintf(remote->name, sizeof(remote->name),
			 "USB Keyspan Remote %04x:%04x",
			 le16_to_cpu(udev->descriptor.idVendor),
			 le16_to_cpu(udev->descriptor.idProduct));

	usb_make_path(udev, remote->phys, sizeof(remote->phys));
	strlcat(remote->phys, "/input0", sizeof(remote->phys));
	memcpy(remote->keymap, keyspan_key_table, sizeof(remote->keymap));

	input_dev->name = remote->name;
	input_dev->phys = remote->phys;
	usb_to_input_id(udev, &input_dev->id);
	input_dev->dev.parent = &interface->dev;
	input_dev->keycode = remote->keymap;
	input_dev->keycodesize = sizeof(unsigned short);
	input_dev->keycodemax = ARRAY_SIZE(remote->keymap);

	input_set_capability(input_dev, EV_MSC, MSC_SCAN);
	__set_bit(EV_KEY, input_dev->evbit);
	for (i = 0; i < ARRAY_SIZE(keyspan_key_table); i++)
		__set_bit(keyspan_key_table[i], input_dev->keybit);
	__clear_bit(KEY_RESERVED, input_dev->keybit);

	input_set_drvdata(input_dev, remote);

	input_dev->open = keyspan_open;
	input_dev->close = keyspan_close;

	/*
	 * Initialize the URB to access the device.
	 * The urb gets sent to the device in keyspan_open()
	 */
	usb_fill_int_urb(remote->irq_urb,
			 remote->udev,
			 usb_rcvintpipe(remote->udev, endpoint->bEndpointAddress),
			 remote->in_buffer, RECV_SIZE, keyspan_irq_recv, remote,
			 endpoint->bInterval);
	remote->irq_urb->transfer_dma = remote->in_dma;
	remote->irq_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* we can register the device now, as it is ready */
	error = input_register_device(remote->input);
	if (error)
		goto fail3;

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, remote);

	return 0;

 fail3:	usb_free_urb(remote->irq_urb);
 fail2:	usb_buffer_free(udev, RECV_SIZE, remote->in_buffer, remote->in_dma);
 fail1:	kfree(remote);
	input_free_device(input_dev);

	return error;
}

/*
 * Routine called when a device is disconnected from the USB.
 */
static void keyspan_disconnect(struct usb_interface *interface)
{
	struct usb_keyspan *remote;

	remote = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (remote) {	/* We have a valid driver structure so clean up everything we allocated. */
		input_unregister_device(remote->input);
		usb_kill_urb(remote->irq_urb);
		usb_free_urb(remote->irq_urb);
		usb_buffer_free(remote->udev, RECV_SIZE, remote->in_buffer, remote->in_dma);
		kfree(remote);
	}
}

/*
 * Standard driver set up sections
 */
static struct usb_driver keyspan_driver =
{
	.name =		"keyspan_remote",
	.probe =	keyspan_probe,
	.disconnect =	keyspan_disconnect,
	.id_table =	keyspan_table
};

static int __init usb_keyspan_init(void)
{
	int result;

	/* register this driver with the USB subsystem */
	result = usb_register(&keyspan_driver);
	if (result)
		err("usb_register failed. Error number %d\n", result);

	return result;
}

static void __exit usb_keyspan_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&keyspan_driver);
}

module_init(usb_keyspan_init);
module_exit(usb_keyspan_exit);

MODULE_DEVICE_TABLE(usb, keyspan_table);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
