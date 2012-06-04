/*
 * lirc_ttusbir.c
 *
 * lirc_ttusbir - LIRC device driver for the TechnoTrend USB IR Receiver
 *
 * Copyright (C) 2007 Stefan Macher <st_maker-lirc@yahoo.de>
 *
 * This LIRC driver provides access to the TechnoTrend USB IR Receiver.
 * The receiver delivers the IR signal as raw sampled true/false data in
 * isochronous USB packets each of size 128 byte.
 * Currently the driver reduces the sampling rate by factor of 8 as this
 * is still more than enough to decode RC-5 - others should be analyzed.
 * But the driver does not rely on RC-5 it should be able to decode every
 * IR signal that is not too fast.
 */

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <media/lirc.h>
#include <media/lirc_dev.h>

MODULE_DESCRIPTION("TechnoTrend USB IR device driver for LIRC");
MODULE_AUTHOR("Stefan Macher (st_maker-lirc@yahoo.de)");
MODULE_LICENSE("GPL");

/* #define DEBUG */
#ifdef DEBUG
#define DPRINTK printk
#else
#define DPRINTK(_x_, a...)
#endif

/* function declarations */
static int probe(struct usb_interface *intf, const struct usb_device_id *id);
static void disconnect(struct usb_interface *intf);
static void urb_complete(struct urb *urb);
static int set_use_inc(void *data);
static void set_use_dec(void *data);

static int num_urbs = 2;
module_param(num_urbs, int, S_IRUGO);
MODULE_PARM_DESC(num_urbs,
		 "Number of URBs in queue. Try to increase to 4 in case "
		 "of problems (default: 2; minimum: 2)");

/* table of devices that work with this driver */
static struct usb_device_id device_id_table[] = {
	/* TechnoTrend USB IR Receiver */
	{ USB_DEVICE(0x0B48, 0x2003) },
	/* Terminating entry */
	{ }
};
MODULE_DEVICE_TABLE(usb, device_id_table);

/* USB driver definition */
static struct usb_driver usb_driver = {
	.name = "TTUSBIR",
	.id_table = &(device_id_table[0]),
	.probe = probe,
	.disconnect = disconnect,
};

/* USB device definition */
struct ttusbir_device {
	struct usb_driver *usb_driver;
	struct usb_device *udev;
	struct usb_interface *interf;
	struct usb_class_driver class_driver;
	unsigned int ifnum; /* Interface number to use */
	unsigned int alt_setting; /* alternate setting to use */
	unsigned int endpoint; /* Endpoint to use */
	struct urb **urb; /* num_urb URB pointers*/
	char **buffer; /* 128 byte buffer for each URB */
	struct lirc_buffer rbuf; /* Buffer towards LIRC */
	struct lirc_driver driver;
	int minor;
	int last_pulse; /* remembers if last received byte was pulse or space */
	int last_num; /* remembers how many last bytes appeared */
	int opened;
};

/*** LIRC specific functions ***/
static int set_use_inc(void *data)
{
	int i, retval;
	struct ttusbir_device *ttusbir = data;

	DPRINTK("Sending first URBs\n");
	/* @TODO Do I need to check if I am already opened */
	ttusbir->opened = 1;

	for (i = 0; i < num_urbs; i++) {
		retval = usb_submit_urb(ttusbir->urb[i], GFP_KERNEL);
		if (retval) {
			dev_err(&ttusbir->interf->dev,
				"%s: usb_submit_urb failed on urb %d\n",
				__func__, i);
			return retval;
		}
	}
	return 0;
}

static void set_use_dec(void *data)
{
	struct ttusbir_device *ttusbir = data;

	DPRINTK("Device closed\n");

	ttusbir->opened = 0;
}

/*** USB specific functions ***/

/*
 * This mapping table is used to do a very simple filtering of the
 * input signal.
 * For a value with at least 4 bits set it returns 0xFF otherwise
 * 0x00.  For faster IR signals this can not be used. But for RC-5 we
 * still have about 14 samples per pulse/space, i.e. we sample with 14
 * times higher frequency than the signal frequency
 */
const unsigned char map_table[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static void urb_complete(struct urb *urb)
{
	struct ttusbir_device *ttusbir;
	unsigned char *buf;
	int i;
	int l;

	ttusbir = urb->context;

	if (!ttusbir->opened)
		return;

	buf = (unsigned char *)urb->transfer_buffer;

	for (i = 0; i < 128; i++) {
		/* Here we do the filtering and some kind of down sampling */
		buf[i] = ~map_table[buf[i]];
		if (ttusbir->last_pulse == buf[i]) {
			if (ttusbir->last_num < PULSE_MASK/63)
				ttusbir->last_num++;
		/*
		 * else we are in a idle period and do not need to
		 * increment any longer
		 */
		} else {
			l = ttusbir->last_num * 62; /* about 62 = us/byte */
			if (ttusbir->last_pulse) /* pulse or space? */
				l |= PULSE_BIT;
			if (!lirc_buffer_full(&ttusbir->rbuf)) {
				lirc_buffer_write(&ttusbir->rbuf, (void *)&l);
				wake_up_interruptible(&ttusbir->rbuf.wait_poll);
			}
			ttusbir->last_num = 0;
			ttusbir->last_pulse = buf[i];
		}
	}
	usb_submit_urb(urb, GFP_ATOMIC); /* keep data rolling :-) */
}

/*
 * Called whenever the USB subsystem thinks we could be the right driver
 * to handle this device
 */
static int probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int alt_set, endp;
	int found = 0;
	int i, j;
	int struct_size;
	struct usb_host_interface *host_interf;
	struct usb_interface_descriptor *interf_desc;
	struct usb_host_endpoint *host_endpoint;
	struct ttusbir_device *ttusbir;

	DPRINTK("Module ttusbir probe\n");

	/* To reduce memory fragmentation we use only one allocation */
	struct_size =  sizeof(struct ttusbir_device) +
		(sizeof(struct urb *) * num_urbs) +
		(sizeof(char *) * num_urbs) +
		(num_urbs * 128);
	ttusbir = kzalloc(struct_size, GFP_KERNEL);
	if (!ttusbir)
		return -ENOMEM;

	ttusbir->urb = (struct urb **)((char *)ttusbir +
				      sizeof(struct ttusbir_device));
	ttusbir->buffer = (char **)((char *)ttusbir->urb +
				   (sizeof(struct urb *) * num_urbs));
	for (i = 0; i < num_urbs; i++)
		ttusbir->buffer[i] = (char *)ttusbir->buffer +
			(sizeof(char *)*num_urbs) + (i * 128);

	ttusbir->usb_driver = &usb_driver;
	ttusbir->alt_setting = -1;
	/* @TODO check if error can be returned */
	ttusbir->udev = usb_get_dev(interface_to_usbdev(intf));
	ttusbir->interf = intf;
	ttusbir->last_pulse = 0x00;
	ttusbir->last_num = 0;

	/*
	 * Now look for interface setting we can handle
	 * We are searching for the alt setting where end point
	 * 0x82 has max packet size 16
	 */
	for (alt_set = 0; alt_set < intf->num_altsetting && !found; alt_set++) {
		host_interf = &intf->altsetting[alt_set];
		interf_desc = &host_interf->desc;
		for (endp = 0; endp < interf_desc->bNumEndpoints; endp++) {
			host_endpoint = &host_interf->endpoint[endp];
			if ((host_endpoint->desc.bEndpointAddress == 0x82) &&
			    (host_endpoint->desc.wMaxPacketSize == 0x10)) {
				ttusbir->alt_setting = alt_set;
				ttusbir->endpoint = endp;
				found = 1;
				break;
			}
		}
	}
	if (ttusbir->alt_setting != -1)
		DPRINTK("alt setting: %d\n", ttusbir->alt_setting);
	else {
		dev_err(&intf->dev, "Could not find alternate setting\n");
		kfree(ttusbir);
		return -EINVAL;
	}

	/* OK lets setup this interface setting */
	usb_set_interface(ttusbir->udev, 0, ttusbir->alt_setting);

	/* Store device info in interface structure */
	usb_set_intfdata(intf, ttusbir);

	/* Register as a LIRC driver */
	if (lirc_buffer_init(&ttusbir->rbuf, sizeof(int), 256) < 0) {
		dev_err(&intf->dev, "Could not get memory for LIRC data buffer\n");
		usb_set_intfdata(intf, NULL);
		kfree(ttusbir);
		return -ENOMEM;
	}
	strcpy(ttusbir->driver.name, "TTUSBIR");
	ttusbir->driver.minor = -1;
	ttusbir->driver.code_length = 1;
	ttusbir->driver.sample_rate = 0;
	ttusbir->driver.data = ttusbir;
	ttusbir->driver.add_to_buf = NULL;
	ttusbir->driver.rbuf = &ttusbir->rbuf;
	ttusbir->driver.set_use_inc = set_use_inc;
	ttusbir->driver.set_use_dec = set_use_dec;
	ttusbir->driver.dev = &intf->dev;
	ttusbir->driver.owner = THIS_MODULE;
	ttusbir->driver.features = LIRC_CAN_REC_MODE2;
	ttusbir->minor = lirc_register_driver(&ttusbir->driver);
	if (ttusbir->minor < 0) {
		dev_err(&intf->dev, "Error registering as LIRC driver\n");
		usb_set_intfdata(intf, NULL);
		lirc_buffer_free(&ttusbir->rbuf);
		kfree(ttusbir);
		return -EIO;
	}

	/* Allocate and setup the URB that we will use to talk to the device */
	for (i = 0; i < num_urbs; i++) {
		ttusbir->urb[i] = usb_alloc_urb(8, GFP_KERNEL);
		if (!ttusbir->urb[i]) {
			dev_err(&intf->dev, "Could not allocate memory for the URB\n");
			for (j = i - 1; j >= 0; j--)
				kfree(ttusbir->urb[j]);
			lirc_buffer_free(&ttusbir->rbuf);
			lirc_unregister_driver(ttusbir->minor);
			kfree(ttusbir);
			usb_set_intfdata(intf, NULL);
			return -ENOMEM;
		}
		ttusbir->urb[i]->dev = ttusbir->udev;
		ttusbir->urb[i]->context = ttusbir;
		ttusbir->urb[i]->pipe = usb_rcvisocpipe(ttusbir->udev,
							ttusbir->endpoint);
		ttusbir->urb[i]->interval = 1;
		ttusbir->urb[i]->transfer_flags = URB_ISO_ASAP;
		ttusbir->urb[i]->transfer_buffer = &ttusbir->buffer[i][0];
		ttusbir->urb[i]->complete = urb_complete;
		ttusbir->urb[i]->number_of_packets = 8;
		ttusbir->urb[i]->transfer_buffer_length = 128;
		for (j = 0; j < 8; j++) {
			ttusbir->urb[i]->iso_frame_desc[j].offset = j*16;
			ttusbir->urb[i]->iso_frame_desc[j].length = 16;
		}
	}
	return 0;
}

/**
 * Called when the driver is unloaded or the device is unplugged
 */
static void disconnect(struct usb_interface *intf)
{
	int i;
	struct ttusbir_device *ttusbir;

	DPRINTK("Module ttusbir disconnect\n");

	ttusbir = (struct ttusbir_device *) usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
	lirc_unregister_driver(ttusbir->minor);
	DPRINTK("unregistered\n");

	for (i = 0; i < num_urbs; i++) {
		usb_kill_urb(ttusbir->urb[i]);
		usb_free_urb(ttusbir->urb[i]);
	}
	DPRINTK("URBs killed\n");
	lirc_buffer_free(&ttusbir->rbuf);
	kfree(ttusbir);
}

module_usb_driver(usb_driver);
