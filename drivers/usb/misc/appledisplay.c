/*
 * Apple Cinema Display driver
 *
 * Copyright (C) 2006  Michael Hanselmann (linux-kernel@hansmi.ch)
 *
 * Thanks to Caskey L. Dickson for his work with acdctl.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/backlight.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <asm/atomic.h>

#define APPLE_VENDOR_ID		0x05AC

#define USB_REQ_GET_REPORT	0x01
#define USB_REQ_SET_REPORT	0x09

#define ACD_USB_TIMEOUT		250

#define ACD_USB_EDID		0x0302
#define ACD_USB_BRIGHTNESS	0x0310

#define ACD_BTN_NONE		0
#define ACD_BTN_BRIGHT_UP	3
#define ACD_BTN_BRIGHT_DOWN	4

#define ACD_URB_BUFFER_LEN	2
#define ACD_MSG_BUFFER_LEN	2

#define APPLEDISPLAY_DEVICE(prod)				\
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE |		\
		       USB_DEVICE_ID_MATCH_INT_CLASS |		\
		       USB_DEVICE_ID_MATCH_INT_PROTOCOL,	\
	.idVendor = APPLE_VENDOR_ID,				\
	.idProduct = (prod),					\
	.bInterfaceClass = USB_CLASS_HID,			\
	.bInterfaceProtocol = 0x00

/* table of devices that work with this driver */
static struct usb_device_id appledisplay_table [] = {
	{ APPLEDISPLAY_DEVICE(0x9218) },
	{ APPLEDISPLAY_DEVICE(0x9219) },
	{ APPLEDISPLAY_DEVICE(0x921d) },

	/* Terminating entry */
	{ }
};
MODULE_DEVICE_TABLE(usb, appledisplay_table);

/* Structure to hold all of our device specific stuff */
struct appledisplay {
	struct usb_device *udev;	/* usb device */
	struct urb *urb;		/* usb request block */
	struct backlight_device *bd;	/* backlight device */
	char *urbdata;			/* interrupt URB data buffer */
	char *msgdata;			/* control message data buffer */

	struct delayed_work work;
	int button_pressed;
	spinlock_t lock;
};

static atomic_t count_displays = ATOMIC_INIT(0);
static struct workqueue_struct *wq;

static void appledisplay_complete(struct urb *urb)
{
	struct appledisplay *pdata = urb->context;
	unsigned long flags;
	int status = urb->status;
	int retval;

	switch (status) {
	case 0:
		/* success */
		break;
	case -EOVERFLOW:
		printk(KERN_ERR "appletouch: OVERFLOW with data "
			"length %d, actual length is %d\n",
			ACD_URB_BUFFER_LEN, pdata->urb->actual_length);
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* This urb is terminated, clean up */
		dbg("%s - urb shuttingdown with status: %d",
			__FUNCTION__, status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
			__FUNCTION__, status);
		goto exit;
	}

	spin_lock_irqsave(&pdata->lock, flags);

	switch(pdata->urbdata[1]) {
	case ACD_BTN_BRIGHT_UP:
	case ACD_BTN_BRIGHT_DOWN:
		pdata->button_pressed = 1;
		queue_delayed_work(wq, &pdata->work, 0);
		break;
	case ACD_BTN_NONE:
	default:
		pdata->button_pressed = 0;
		break;
	}

	spin_unlock_irqrestore(&pdata->lock, flags);

exit:
	retval = usb_submit_urb(pdata->urb, GFP_ATOMIC);
	if (retval) {
		err("%s - usb_submit_urb failed with result %d",
			__FUNCTION__, retval);
	}
}

static int appledisplay_bl_update_status(struct backlight_device *bd)
{
	struct appledisplay *pdata = bl_get_data(bd);
	int retval;

	pdata->msgdata[0] = 0x10;
	pdata->msgdata[1] = bd->props.brightness;

	retval = usb_control_msg(
		pdata->udev,
		usb_sndctrlpipe(pdata->udev, 0),
		USB_REQ_SET_REPORT,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		ACD_USB_BRIGHTNESS,
		0,
		pdata->msgdata, 2,
		ACD_USB_TIMEOUT);

	return retval;
}

static int appledisplay_bl_get_brightness(struct backlight_device *bd)
{
	struct appledisplay *pdata = bl_get_data(bd);
	int retval;

	retval = usb_control_msg(
		pdata->udev,
		usb_rcvctrlpipe(pdata->udev, 0),
		USB_REQ_GET_REPORT,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		ACD_USB_BRIGHTNESS,
		0,
		pdata->msgdata, 2,
		ACD_USB_TIMEOUT);

	if (retval < 0)
		return retval;
	else
		return pdata->msgdata[1];
}

static struct backlight_ops appledisplay_bl_data = {
	.get_brightness	= appledisplay_bl_get_brightness,
	.update_status	= appledisplay_bl_update_status,
};

static void appledisplay_work(struct work_struct *work)
{
	struct appledisplay *pdata =
		container_of(work, struct appledisplay, work.work);
	int retval;

	retval = appledisplay_bl_get_brightness(pdata->bd);
	if (retval >= 0)
		pdata->bd->props.brightness = retval;

	/* Poll again in about 125ms if there's still a button pressed */
	if (pdata->button_pressed)
		schedule_delayed_work(&pdata->work, HZ / 8);
}

static int appledisplay_probe(struct usb_interface *iface,
	const struct usb_device_id *id)
{
	struct appledisplay *pdata;
	struct usb_device *udev = interface_to_usbdev(iface);
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int int_in_endpointAddr = 0;
	int i, retval = -ENOMEM, brightness;
	char bl_name[20];

	/* set up the endpoint information */
	/* use only the first interrupt-in endpoint */
	iface_desc = iface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (!int_in_endpointAddr && usb_endpoint_is_int_in(endpoint)) {
			/* we found an interrupt in endpoint */
			int_in_endpointAddr = endpoint->bEndpointAddress;
			break;
		}
	}
	if (!int_in_endpointAddr) {
		err("Could not find int-in endpoint");
		return -EIO;
	}

	/* allocate memory for our device state and initialize it */
	pdata = kzalloc(sizeof(struct appledisplay), GFP_KERNEL);
	if (!pdata) {
		retval = -ENOMEM;
		err("Out of memory");
		goto error;
	}

	pdata->udev = udev;

	spin_lock_init(&pdata->lock);
	INIT_DELAYED_WORK(&pdata->work, appledisplay_work);

	/* Allocate buffer for control messages */
	pdata->msgdata = kmalloc(ACD_MSG_BUFFER_LEN, GFP_KERNEL);
	if (!pdata->msgdata) {
		retval = -ENOMEM;
		err("appledisplay: Allocating buffer for control messages "
			"failed");
		goto error;
	}

	/* Allocate interrupt URB */
	pdata->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pdata->urb) {
		retval = -ENOMEM;
		err("appledisplay: Allocating URB failed");
		goto error;
	}

	/* Allocate buffer for interrupt data */
	pdata->urbdata = usb_buffer_alloc(pdata->udev, ACD_URB_BUFFER_LEN,
		GFP_KERNEL, &pdata->urb->transfer_dma);
	if (!pdata->urbdata) {
		retval = -ENOMEM;
		err("appledisplay: Allocating URB buffer failed");
		goto error;
	}

	/* Configure interrupt URB */
	usb_fill_int_urb(pdata->urb, udev,
		usb_rcvintpipe(udev, int_in_endpointAddr),
		pdata->urbdata, ACD_URB_BUFFER_LEN, appledisplay_complete,
		pdata, 1);
	if (usb_submit_urb(pdata->urb, GFP_KERNEL)) {
		retval = -EIO;
		err("appledisplay: Submitting URB failed");
		goto error;
	}

	/* Register backlight device */
	snprintf(bl_name, sizeof(bl_name), "appledisplay%d",
		atomic_inc_return(&count_displays) - 1);
	pdata->bd = backlight_device_register(bl_name, NULL, pdata,
						&appledisplay_bl_data);
	if (IS_ERR(pdata->bd)) {
		err("appledisplay: Backlight registration failed");
		goto error;
	}

	pdata->bd->props.max_brightness = 0xff;

	/* Try to get brightness */
	brightness = appledisplay_bl_get_brightness(pdata->bd);

	if (brightness < 0) {
		retval = brightness;
		err("appledisplay: Error while getting initial brightness: %d", retval);
		goto error;
	}

	/* Set brightness in backlight device */
	pdata->bd->props.brightness = brightness;

	/* save our data pointer in the interface device */
	usb_set_intfdata(iface, pdata);

	printk(KERN_INFO "appledisplay: Apple Cinema Display connected\n");

	return 0;

error:
	if (pdata) {
		if (pdata->urb) {
			usb_kill_urb(pdata->urb);
			if (pdata->urbdata)
				usb_buffer_free(pdata->udev, ACD_URB_BUFFER_LEN,
					pdata->urbdata, pdata->urb->transfer_dma);
			usb_free_urb(pdata->urb);
		}
		if (pdata->bd)
			backlight_device_unregister(pdata->bd);
		kfree(pdata->msgdata);
	}
	usb_set_intfdata(iface, NULL);
	kfree(pdata);
	return retval;
}

static void appledisplay_disconnect(struct usb_interface *iface)
{
	struct appledisplay *pdata = usb_get_intfdata(iface);

	if (pdata) {
		usb_kill_urb(pdata->urb);
		cancel_delayed_work(&pdata->work);
		backlight_device_unregister(pdata->bd);
		usb_buffer_free(pdata->udev, ACD_URB_BUFFER_LEN,
			pdata->urbdata, pdata->urb->transfer_dma);
		usb_free_urb(pdata->urb);
		kfree(pdata->msgdata);
		kfree(pdata);
	}

	printk(KERN_INFO "appledisplay: Apple Cinema Display disconnected\n");
}

static struct usb_driver appledisplay_driver = {
	.name		= "appledisplay",
	.probe		= appledisplay_probe,
	.disconnect	= appledisplay_disconnect,
	.id_table	= appledisplay_table,
};

static int __init appledisplay_init(void)
{
	wq = create_singlethread_workqueue("appledisplay");
	if (!wq) {
		err("Could not create work queue\n");
		return -ENOMEM;
	}

	return usb_register(&appledisplay_driver);
}

static void __exit appledisplay_exit(void)
{
	flush_workqueue(wq);
	destroy_workqueue(wq);
	usb_deregister(&appledisplay_driver);
}

MODULE_AUTHOR("Michael Hanselmann");
MODULE_DESCRIPTION("Apple Cinema Display driver");
MODULE_LICENSE("GPL");

module_init(appledisplay_init);
module_exit(appledisplay_exit);
