/*
   tm6000-cards.c - driver for TM5600/TM6000 USB video capture devices

   Copyright (C) 2006-2007 Mauro Carvalho Chehab <mchehab@infradead.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/usb.h>
#include <linux/version.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>

#include "tm6000.h"
#include "tm6000-regs.h"

#define TM6000_BOARD_UNKNOWN			0
#define TM5600_BOARD_GENERIC			1
#define TM6000_BOARD_GENERIC			2
#define TM5600_BOARD_10MOONS_UT821		3
#define TM5600_BOARD_10MOONS_UT330		4
#define TM6000_BOARD_ADSTECH_DUAL_TV		5
#define TM6000_BOARD_FREECOM_AND_SIMILAR	6

#define TM6000_MAXBOARDS        16
static unsigned int card[]     = {[0 ... (TM6000_MAXBOARDS - 1)] = UNSET };

module_param_array(card,  int, NULL, 0444);


struct tm6000_board {
	char            *name;

	struct tm6000_capabilities caps;

	int             tuner_type;     /* type of the tuner */
	int             tuner_addr;     /* tuner address */
	int             demod_addr;     /* demodulator address */
	int		gpio_addr_tun_reset;	/* GPIO used for tuner reset */
};


struct tm6000_board tm6000_boards[] = {
	[TM6000_BOARD_UNKNOWN] = {
		.name         = "Unknown tm6000 video grabber",
		.caps = {
			.has_tuner    = 1,
		},
		.gpio_addr_tun_reset = TM6000_GPIO_1,
	},
	[TM5600_BOARD_GENERIC] = {
		.name         = "Generic tm5600 board",
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc2,
		.caps = {
			.has_tuner	= 1,
		},
		.gpio_addr_tun_reset = TM6000_GPIO_1,
	},
	[TM6000_BOARD_GENERIC] = {
		.name         = "Generic tm6000 board",
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc2,
		.caps = {
			.has_tuner	= 1,
			.has_dvb	= 1,
		},
		.gpio_addr_tun_reset = TM6000_GPIO_1,
	},
	[TM5600_BOARD_10MOONS_UT821] = {
		.name         = "10Moons UT 821",
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc2,
		.caps = {
			.has_tuner    = 1,
			.has_eeprom   = 1,
		},
		.gpio_addr_tun_reset = TM6000_GPIO_1,
	},
	[TM5600_BOARD_10MOONS_UT330] = {
		.name         = "10Moons UT 330",
		.tuner_type   = TUNER_PHILIPS_FQ1216AME_MK4,
		.tuner_addr   = 0xc8,
		.caps = {
			.has_tuner    = 1,
			.has_dvb      = 0,
			.has_zl10353  = 0,
			.has_eeprom   = 1,
		},
	},
	[TM6000_BOARD_ADSTECH_DUAL_TV] = {
		.name         = "ADSTECH Dual TV USB",
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc8,
		.caps = {
			.has_tuner    = 1,
			.has_tda9874  = 1,
			.has_dvb      = 1,
			.has_zl10353  = 1,
			.has_eeprom   = 1,
		},
	},
	[TM6000_BOARD_FREECOM_AND_SIMILAR] = {
		.name         = "Freecom Hybrid Stick / Moka DVB-T Receiver Dual",
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc2,
		.demod_addr   = 0x1e,
		.caps = {
			.has_tuner    = 1,
			.has_dvb      = 1,
			.has_zl10353  = 1,
			.has_eeprom   = 0,
		},
		.gpio_addr_tun_reset = TM6000_GPIO_4,
	},
};

/* table of devices that work with this driver */
struct usb_device_id tm6000_id_table [] = {
	{ USB_DEVICE(0x6000, 0x0001), .driver_info = TM5600_BOARD_10MOONS_UT821 },
	{ USB_DEVICE(0x06e1, 0xf332), .driver_info = TM6000_BOARD_ADSTECH_DUAL_TV },
	{ USB_DEVICE(0x14aa, 0x0620), .driver_info = TM6000_BOARD_FREECOM_AND_SIMILAR },
	{ },
};

static int tm6000_init_dev(struct tm6000_core *dev)
{
	struct v4l2_frequency f;
	int rc = 0;

	mutex_init(&dev->lock);

	mutex_lock(&dev->lock);

	/* Initializa board-specific data */
	dev->tuner_type = tm6000_boards[dev->model].tuner_type;
	dev->tuner_addr = tm6000_boards[dev->model].tuner_addr;
	dev->tuner_reset_gpio = tm6000_boards[dev->model].gpio_addr_tun_reset;

	dev->demod_addr = tm6000_boards[dev->model].demod_addr;

	dev->caps = tm6000_boards[dev->model].caps;

	/* initialize hardware */
	rc=tm6000_init (dev);
	if (rc<0)
		goto err;

	/* register i2c bus */
	rc=tm6000_i2c_register(dev);
	if (rc<0)
		goto err;

	/* register and initialize V4L2 */
	rc=tm6000_v4l2_register(dev);
	if (rc<0)
		goto err;

	/* Request tuner */
	request_module ("tuner");
//	norm=V4L2_STD_NTSC_M;
	dev->norm=V4L2_STD_PAL_M;
	tm6000_i2c_call_clients(dev, VIDIOC_S_STD, &dev->norm);

	/* configure tuner */
	f.tuner = 0;
	f.type = V4L2_TUNER_ANALOG_TV;
	f.frequency = 3092;	/* 193.25 MHz */
	dev->freq = f.frequency;

	tm6000_i2c_call_clients(dev, VIDIOC_S_FREQUENCY, &f);

err:
	mutex_unlock(&dev->lock);
	return rc;
}

/* high bandwidth multiplier, as encoded in highspeed endpoint descriptors */
#define hb_mult(wMaxPacketSize) (1 + (((wMaxPacketSize) >> 11) & 0x03))

static void get_max_endpoint (  struct usb_device *usbdev,
				char *msgtype,
				struct usb_host_endpoint *curr_e,
				unsigned int *maxsize,
				struct usb_host_endpoint **ep  )
{
	u16 tmp = le16_to_cpu(curr_e->desc.wMaxPacketSize);
	unsigned int size = tmp & 0x7ff;

	if (usbdev->speed == USB_SPEED_HIGH)
		size = size * hb_mult (tmp);

	if (size>*maxsize) {
		*ep = curr_e;
		*maxsize = size;
		printk("tm6000: %s endpoint: 0x%02x (max size=%u bytes)\n",
					msgtype, curr_e->desc.bEndpointAddress,
					size);
	}
}

/*
 * tm6000_usb_probe()
 * checks for supported devices
 */
static int tm6000_usb_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	struct usb_device *usbdev;
	struct tm6000_core *dev = NULL;
	int i,rc=0;
	int nr=0;
	char *speed;


	usbdev=usb_get_dev(interface_to_usbdev(interface));

	/* Selects the proper interface */
	rc=usb_set_interface(usbdev,0,1);
	if (rc<0)
		goto err;

	/* Check to see next free device and mark as used */
	nr=find_first_zero_bit(&tm6000_devused,TM6000_MAXBOARDS);
	if (nr >= TM6000_MAXBOARDS) {
		printk ("tm6000: Supports only %i em28xx boards.\n",TM6000_MAXBOARDS);
		usb_put_dev(usbdev);
		return -ENOMEM;
	}

	/* Create and initialize dev struct */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		printk ("tm6000" ": out of memory!\n");
		usb_put_dev(usbdev);
		return -ENOMEM;
	}
	spin_lock_init(&dev->slock);

	/* Increment usage count */
	tm6000_devused|=1<<nr;
	snprintf(dev->name, 29, "tm6000 #%d", nr);

	dev->model=id->driver_info;
	if ((card[nr]>=0) && (card[nr]<ARRAY_SIZE(tm6000_boards))) {
		dev->model=card[nr];
	}

	INIT_LIST_HEAD(&dev->tm6000_corelist);
	dev->udev= usbdev;
	dev->devno=nr;

	switch (usbdev->speed) {
	case USB_SPEED_LOW:
		speed = "1.5";
		break;
	case USB_SPEED_UNKNOWN:
	case USB_SPEED_FULL:
		speed = "12";
		break;
	case USB_SPEED_HIGH:
		speed = "480";
		break;
	default:
		speed = "unknown";
	}



	/* Get endpoints */
	for (i = 0; i < interface->num_altsetting; i++) {
		int ep;

		for (ep = 0; ep < interface->altsetting[i].desc.bNumEndpoints; ep++) {
			struct usb_host_endpoint	*e;
			int dir_out;

			e = &interface->altsetting[i].endpoint[ep];

			dir_out = ((e->desc.bEndpointAddress &
					USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT);

			printk("tm6000: alt %d, interface %i, class %i\n",
			       i,
			       interface->altsetting[i].desc.bInterfaceNumber,
			       interface->altsetting[i].desc.bInterfaceClass);

			switch (e->desc.bmAttributes) {
			case USB_ENDPOINT_XFER_BULK:
				if (!dir_out) {
					get_max_endpoint (usbdev, "Bulk IN", e,
							&dev->max_bulk_in,
							&dev->bulk_in);
				} else {
					get_max_endpoint (usbdev, "Bulk OUT", e,
							&dev->max_bulk_out,
							&dev->bulk_out);
				}
				break;
			case USB_ENDPOINT_XFER_ISOC:
				if (!dir_out) {
					get_max_endpoint (usbdev, "ISOC IN", e,
							&dev->max_isoc_in,
							&dev->isoc_in);
				} else {
					get_max_endpoint (usbdev, "ISOC OUT", e,
							&dev->max_isoc_out,
							&dev->isoc_out);
				}
				break;
			}
		}
	}

	if (interface->altsetting->desc.bAlternateSetting) {
		printk("selecting alt setting %d\n",
		       interface->altsetting->desc.bAlternateSetting);
		rc = usb_set_interface (usbdev,
				interface->altsetting->desc.bInterfaceNumber,
				interface->altsetting->desc.bAlternateSetting);
		if (rc<0)
			goto err;
	}

	printk("tm6000: New video device @ %s Mbps (%04x:%04x, ifnum %d)\n",
		speed,
		le16_to_cpu(dev->udev->descriptor.idVendor),
		le16_to_cpu(dev->udev->descriptor.idProduct),
		interface->altsetting->desc.bInterfaceNumber);

/* check if the the device has the iso in endpoint at the correct place */
	if (!dev->isoc_in) {
		printk("tm6000: probing error: no IN ISOC endpoint!\n");
		rc= -ENODEV;

		goto err;
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	printk("tm6000: Found %s\n", tm6000_boards[dev->model].name);

	rc=tm6000_init_dev(dev);

	if (rc<0)
		goto err;

	return 0;

err:
	tm6000_devused&=~(1<<nr);
	usb_put_dev(usbdev);

	kfree(dev);
	return rc;
}

/*
 * tm6000_usb_disconnect()
 * called when the device gets diconencted
 * video device will be unregistered on v4l2_close in case it is still open
 */
static void tm6000_usb_disconnect(struct usb_interface *interface)
{
	struct tm6000_core *dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (!dev)
		return;

	printk("tm6000: disconnecting %s\n", dev->name);

	mutex_lock(&dev->lock);

	tm6000_v4l2_unregister(dev);

	tm6000_i2c_unregister(dev);

//	wake_up_interruptible_all(&dev->open);

	dev->state |= DEV_DISCONNECTED;

	usb_put_dev(dev->udev);

	mutex_unlock(&dev->lock);
	kfree(dev);
}

static struct usb_driver tm6000_usb_driver = {
		.name = "tm6000",
		.probe = tm6000_usb_probe,
		.disconnect = tm6000_usb_disconnect,
		.id_table = tm6000_id_table,
};

static int __init tm6000_module_init(void)
{
	int result;

	printk(KERN_INFO "tm6000" " v4l2 driver version %d.%d.%d loaded\n",
	       (TM6000_VERSION  >> 16) & 0xff,
	       (TM6000_VERSION  >> 8) & 0xff, TM6000_VERSION  & 0xff);

	/* register this driver with the USB subsystem */
	result = usb_register(&tm6000_usb_driver);
	if (result)
		printk("tm6000"
			   " usb_register failed. Error number %d.\n", result);

	return result;
}

static void __exit tm6000_module_exit(void)
{
	/* deregister at USB subsystem */
	usb_deregister(&tm6000_usb_driver);
}

module_init(tm6000_module_init);
module_exit(tm6000_module_exit);

MODULE_DESCRIPTION("Trident TVMaster TM5600/TM6000 USB2 adapter");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_LICENSE("GPL");
