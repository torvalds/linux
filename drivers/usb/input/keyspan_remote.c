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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/input.h>
#include <linux/usb.h>

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
	struct usb_device*		udev;
	struct input_dev		input;
	struct usb_interface*		interface;
	struct usb_endpoint_descriptor* in_endpoint;
	struct urb*			irq_urb;
	int				open;
	dma_addr_t			in_dma;
	unsigned char*			in_buffer;

	/* variables used to parse messages from remote. */
	struct bit_tester		data;
	int				stage;
	int				toggle;
};

/*
 * Table that maps the 31 possible keycodes to input keys.
 * Currently there are 15 and 17 button models so RESERVED codes
 * are blank areas in the mapping.
 */
static int keyspan_key_table[] = {
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

static struct usb_driver keyspan_driver;

/*
 * Debug routine that prints out what we've received from the remote.
 */
static void keyspan_print(struct usb_keyspan* dev) /*unsigned char* data)*/
{
	char codes[4*RECV_SIZE];
	int i;

	for (i = 0; i < RECV_SIZE; i++) {
		snprintf(codes+i*3, 4, "%02x ", dev->in_buffer[i]);
	}

	dev_info(&dev->udev->dev, "%s\n", codes);
}

/*
 * Routine that manages the bit_tester structure.  It makes sure that there are
 * at least bits_needed bits loaded into the tester.
 */
static int keyspan_load_tester(struct usb_keyspan* dev, int bits_needed)
{
	if (dev->data.bits_left >= bits_needed)
		return(0);

	/*
	 * Somehow we've missed the last message. The message will be repeated
	 * though so it's not too big a deal
	 */
	if (dev->data.pos >= dev->data.len) {
		dev_dbg(&dev->udev, "%s - Error ran out of data. pos: %d, len: %d\n",
			__FUNCTION__, dev->data.pos, dev->data.len);
		return(-1);
	}

	/* Load as much as we can into the tester. */
	while ((dev->data.bits_left + 7 < (sizeof(dev->data.tester) * 8)) &&
	       (dev->data.pos < dev->data.len)) {
		dev->data.tester += (dev->data.buffer[dev->data.pos++] << dev->data.bits_left);
		dev->data.bits_left += 8;
	}

	return(0);
}

/*
 * Routine that handles all the logic needed to parse out the message from the remote.
 */
static void keyspan_check_data(struct usb_keyspan *remote, struct pt_regs *regs)
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
				err("%s - Unknown sequence found in system data.\n", __FUNCTION__);
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
				err("%s - Unknown sequence found in button data.\n", __FUNCTION__);
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
			err("%s - Error in message, invalid toggle.\n", __FUNCTION__);
		}

		keyspan_load_tester(remote, 5);
		if ((remote->data.tester & STOP_MASK) == STOP) {
			remote->data.tester = remote->data.tester >> 5;
			remote->data.bits_left -= 5;
		} else {
			err("Bad message recieved, no stop bit found.\n");
		}

		dev_dbg(&remote->udev,
			"%s found valid message: system: %d, button: %d, toggle: %d\n",
			__FUNCTION__, message.system, message.button, message.toggle);

		if (message.toggle != remote->toggle) {
			input_regs(&remote->input, regs);
			input_report_key(&remote->input, keyspan_key_table[message.button], 1);
			input_report_key(&remote->input, keyspan_key_table[message.button], 0);
			input_sync(&remote->input);
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
			__FUNCTION__, retval);
		return(retval);
	}

	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 0x44, 0x40, 0x0, 0x0, NULL, 0, 0);
	if (retval) {
		dev_dbg(&dev->dev, "%s - failed to set resume sensitivity due to error: %d\n",
			__FUNCTION__, retval);
		return(retval);
	}

	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 0x22, 0x40, 0x0, 0x0, NULL, 0, 0);
	if (retval) {
		dev_dbg(&dev->dev, "%s - failed to turn receive on due to error: %d\n",
			__FUNCTION__, retval);
		return(retval);
	}

	dev_dbg(&dev->dev, "%s - Setup complete.\n", __FUNCTION__);
	return(retval);
}

/*
 * Routine used to handle a new message that has come in.
 */
static void keyspan_irq_recv(struct urb *urb, struct pt_regs *regs)
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

	keyspan_check_data(dev, regs);

resubmit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		err ("%s - usb_submit_urb failed with result: %d", __FUNCTION__, retval);
}

static int keyspan_open(struct input_dev *dev)
{
	struct usb_keyspan *remote = dev->private;

	if (remote->open++)
		return 0;

	remote->irq_urb->dev = remote->udev;
	if (usb_submit_urb(remote->irq_urb, GFP_KERNEL)) {
		remote->open--;
		return -EIO;
	}

	return 0;
}

static void keyspan_close(struct input_dev *dev)
{
	struct usb_keyspan *remote = dev->private;

	if (!--remote->open)
		usb_kill_urb(remote->irq_urb);
}

/*
 * Routine that sets up the driver to handle a specific USB device detected on the bus.
 */
static int keyspan_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int i;
	int retval = -ENOMEM;
	char path[64];
	char *buf;
	struct usb_keyspan *remote = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *udev = usb_get_dev(interface_to_usbdev(interface));

	/* allocate memory for our device state and initialize it */
	remote = kmalloc(sizeof(*remote), GFP_KERNEL);
	if (remote == NULL) {
		err("Out of memory\n");
		goto error;
	}
	memset(remote, 0x00, sizeof(*remote));

	remote->udev = udev;
	remote->interface = interface;
	remote->toggle = -1;	/* Set to -1 so we will always not match the toggle from the first remote message. */

	/* set up the endpoint information */
	/* use only the first in interrupt endpoint */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!remote->in_endpoint &&
		    (endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)) {
			/* we found our interrupt in endpoint */
			remote->in_endpoint = endpoint;

			remote->in_buffer = usb_buffer_alloc(remote->udev, RECV_SIZE, SLAB_ATOMIC, &remote->in_dma);
			if (!remote->in_buffer) {
				retval = -ENOMEM;
				goto error;
			}
		}
	}

	if (!remote->in_endpoint) {
		err("Could not find interrupt input endpoint.\n");
		retval = -ENODEV;
		goto error;
	}

	remote->irq_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!remote->irq_urb) {
		err("Failed to allocate urb.\n");
		retval = -ENOMEM;
		goto error;
	}

	retval = keyspan_setup(remote->udev);
	if (retval) {
		err("Failed to setup device.\n");
		retval = -ENODEV;
		goto error;
	}

	/*
	 * Setup the input system with the bits we are going to be reporting
	 */
	remote->input.evbit[0] = BIT(EV_KEY);		/* We will only report KEY events. */
	for (i = 0; i < 32; ++i) {
		if (keyspan_key_table[i] != KEY_RESERVED) {
			set_bit(keyspan_key_table[i], remote->input.keybit);
		}
	}

	remote->input.private = remote;
	remote->input.open = keyspan_open;
	remote->input.close = keyspan_close;

	usb_make_path(remote->udev, path, 64);
	sprintf(remote->phys, "%s/input0", path);

	remote->input.name = remote->name;
	remote->input.phys = remote->phys;
	remote->input.id.bustype = BUS_USB;
	remote->input.id.vendor = le16_to_cpu(remote->udev->descriptor.idVendor);
	remote->input.id.product = le16_to_cpu(remote->udev->descriptor.idProduct);
	remote->input.id.version = le16_to_cpu(remote->udev->descriptor.bcdDevice);

	if (!(buf = kmalloc(63, GFP_KERNEL))) {
		usb_buffer_free(remote->udev, RECV_SIZE, remote->in_buffer, remote->in_dma);
		kfree(remote);
		return -ENOMEM;
	}

	if (remote->udev->descriptor.iManufacturer &&
	    usb_string(remote->udev, remote->udev->descriptor.iManufacturer, buf, 63) > 0)
		strcat(remote->name, buf);

	if (remote->udev->descriptor.iProduct &&
	    usb_string(remote->udev, remote->udev->descriptor.iProduct, buf, 63) > 0)
		sprintf(remote->name, "%s %s", remote->name, buf);

	if (!strlen(remote->name))
		sprintf(remote->name, "USB Keyspan Remote %04x:%04x",
			remote->input.id.vendor, remote->input.id.product);

	kfree(buf);

	/*
	 * Initialize the URB to access the device. The urb gets sent to the device in keyspan_open()
	 */
	usb_fill_int_urb(remote->irq_urb,
			 remote->udev, usb_rcvintpipe(remote->udev, remote->in_endpoint->bEndpointAddress),
			 remote->in_buffer, RECV_SIZE, keyspan_irq_recv, remote,
			 remote->in_endpoint->bInterval);
	remote->irq_urb->transfer_dma = remote->in_dma;
	remote->irq_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* we can register the device now, as it is ready */
	input_register_device(&remote->input);

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, remote);

	/* let the user know what node this device is now attached to */
	info("connected: %s on %s", remote->name, path);
	return 0;

error:
	/*
	 * In case of error we need to clean up any allocated buffers
	 */
	if (remote->irq_urb)
		usb_free_urb(remote->irq_urb);

	if (remote->in_buffer)
		usb_buffer_free(remote->udev, RECV_SIZE, remote->in_buffer, remote->in_dma);

	if (remote)
		kfree(remote);

	return retval;
}

/*
 * Routine called when a device is disconnected from the USB.
 */
static void keyspan_disconnect(struct usb_interface *interface)
{
	struct usb_keyspan *remote;

	/* prevent keyspan_open() from racing keyspan_disconnect() */
	lock_kernel();

	remote = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (remote) {	/* We have a valid driver structure so clean up everything we allocated. */
		input_unregister_device(&remote->input);
		usb_kill_urb(remote->irq_urb);
		usb_free_urb(remote->irq_urb);
		usb_buffer_free(interface_to_usbdev(interface), RECV_SIZE, remote->in_buffer, remote->in_dma);
		kfree(remote);
	}

	unlock_kernel();

	info("USB Keyspan now disconnected");
}

/*
 * Standard driver set up sections
 */
static struct usb_driver keyspan_driver =
{
	.owner =	THIS_MODULE,
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
