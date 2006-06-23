/******************************************************************************
 * itmtouch.c  --  Driver for ITM touchscreen panel
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Based upon original work by Chris Collins <xfire-itmtouch@xware.cx>.
 *
 * Kudos to ITM for providing me with the datasheet for the panel,
 * even though it was a day later than I had finished writing this
 * driver.
 *
 * It has meant that I've been able to correct my interpretation of the
 * protocol packets however.
 *
 * CC -- 2003/9/29
 *
 * History
 * 1.0 & 1.1  2003 (CC) vojtech@suse.cz
 *   Original version for 2.4.x kernels
 *
 * 1.2  02/03/2005 (HCE) hc@mivu.no
 *   Complete rewrite to support Linux 2.6.10, thanks to mtouchusb.c for hints.
 *   Unfortunately no calibration support at this time.
 *
 * 1.2.1  09/03/2005 (HCE) hc@mivu.no
 *   Code cleanup and adjusting syntax to start matching kernel standards
 *
 *****************************************************************************/

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>

/* only an 8 byte buffer necessary for a single packet */
#define ITM_BUFSIZE			8
#define PATH_SIZE			64

#define USB_VENDOR_ID_ITMINC		0x0403
#define USB_PRODUCT_ID_TOUCHPANEL	0xf9e9

#define DRIVER_AUTHOR "Hans-Christian Egtvedt <hc@mivu.no>"
#define DRIVER_VERSION "v1.2.1"
#define DRIVER_DESC "USB ITM Inc Touch Panel Driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE( DRIVER_LICENSE );

struct itmtouch_dev {
	struct usb_device	*usbdev; /* usb device */
	struct input_dev	*inputdev; /* input device */
	struct urb		*readurb; /* urb */
	char			rbuf[ITM_BUFSIZE]; /* data */
	int			users;
	char name[128];
	char phys[64];
};

static struct usb_device_id itmtouch_ids [] = {
	{ USB_DEVICE(USB_VENDOR_ID_ITMINC, USB_PRODUCT_ID_TOUCHPANEL) },
	{ }
};

static void itmtouch_irq(struct urb *urb, struct pt_regs *regs)
{
	struct itmtouch_dev *itmtouch = urb->context;
	unsigned char *data = urb->transfer_buffer;
	struct input_dev *dev = itmtouch->inputdev;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ETIMEDOUT:
		/* this urb is timing out */
		dbg("%s - urb timed out - was the device unplugged?",
		    __FUNCTION__);
		return;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
		    __FUNCTION__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
		    __FUNCTION__, urb->status);
		goto exit;
	}

	input_regs(dev, regs);

	/* if pressure has been released, then don't report X/Y */
	if (data[7] & 0x20) {
		input_report_abs(dev, ABS_X, (data[0] & 0x1F) << 7 | (data[3] & 0x7F));
		input_report_abs(dev, ABS_Y, (data[1] & 0x1F) << 7 | (data[4] & 0x7F));
	}

	input_report_abs(dev, ABS_PRESSURE, (data[2] & 1) << 7 | (data[5] & 0x7F));
	input_report_key(dev, BTN_TOUCH, ~data[7] & 0x20);
	input_sync(dev);

exit:
	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		printk(KERN_ERR "%s - usb_submit_urb failed with result: %d",
				__FUNCTION__, retval);
}

static int itmtouch_open(struct input_dev *input)
{
	struct itmtouch_dev *itmtouch = input->private;

	itmtouch->readurb->dev = itmtouch->usbdev;

	if (usb_submit_urb(itmtouch->readurb, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void itmtouch_close(struct input_dev *input)
{
	struct itmtouch_dev *itmtouch = input->private;

	usb_kill_urb(itmtouch->readurb);
}

static int itmtouch_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct itmtouch_dev *itmtouch;
	struct input_dev *input_dev;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *udev = interface_to_usbdev(intf);
	unsigned int pipe;
	unsigned int maxp;

	interface = intf->cur_altsetting;
	endpoint = &interface->endpoint[0].desc;

	itmtouch = kzalloc(sizeof(struct itmtouch_dev), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!itmtouch || !input_dev) {
		err("%s - Out of memory.", __FUNCTION__);
		goto fail;
	}

	itmtouch->usbdev = udev;
	itmtouch->inputdev = input_dev;

	if (udev->manufacturer)
		strlcpy(itmtouch->name, udev->manufacturer, sizeof(itmtouch->name));

	if (udev->product) {
		if (udev->manufacturer)
			strlcat(itmtouch->name, " ", sizeof(itmtouch->name));
		strlcat(itmtouch->name, udev->product, sizeof(itmtouch->name));
	}

	if (!strlen(itmtouch->name))
		sprintf(itmtouch->name, "USB ITM touchscreen");

	usb_make_path(udev, itmtouch->phys, sizeof(itmtouch->phys));
	strlcpy(itmtouch->phys, "/input0", sizeof(itmtouch->phys));

	input_dev->name = itmtouch->name;
	input_dev->phys = itmtouch->phys;
	usb_to_input_id(udev, &input_dev->id);
	input_dev->cdev.dev = &intf->dev;
	input_dev->private = itmtouch;

	input_dev->open = itmtouch_open;
	input_dev->close = itmtouch_close;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);
	input_dev->keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);

	/* device limits */
	/* as specified by the ITM datasheet, X and Y are 12bit,
	 * Z (pressure) is 8 bit. However, the fields are defined up
	 * to 14 bits for future possible expansion.
	 */
	input_set_abs_params(input_dev, ABS_X, 0, 0x0FFF, 2, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 0x0FFF, 2, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 0xFF, 2, 0);

	/* initialise the URB so we can read from the transport stream */
	pipe = usb_rcvintpipe(itmtouch->usbdev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));

	if (maxp > ITM_BUFSIZE)
		maxp = ITM_BUFSIZE;

	itmtouch->readurb = usb_alloc_urb(0, GFP_KERNEL);
	if (!itmtouch->readurb) {
		dbg("%s - usb_alloc_urb failed: itmtouch->readurb", __FUNCTION__);
		goto fail;
	}

	usb_fill_int_urb(itmtouch->readurb, itmtouch->usbdev, pipe, itmtouch->rbuf,
			 maxp, itmtouch_irq, itmtouch, endpoint->bInterval);

	input_register_device(itmtouch->inputdev);

	usb_set_intfdata(intf, itmtouch);

	return 0;

 fail:	input_free_device(input_dev);
	kfree(itmtouch);
	return -ENOMEM;
}

static void itmtouch_disconnect(struct usb_interface *intf)
{
	struct itmtouch_dev *itmtouch = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (itmtouch) {
		input_unregister_device(itmtouch->inputdev);
		usb_kill_urb(itmtouch->readurb);
		usb_free_urb(itmtouch->readurb);
		kfree(itmtouch);
	}
}

MODULE_DEVICE_TABLE(usb, itmtouch_ids);

static struct usb_driver itmtouch_driver = {
	.name =         "itmtouch",
	.probe =        itmtouch_probe,
	.disconnect =   itmtouch_disconnect,
	.id_table =     itmtouch_ids,
};

static int __init itmtouch_init(void)
{
	info(DRIVER_DESC " " DRIVER_VERSION);
	info(DRIVER_AUTHOR);
	return usb_register(&itmtouch_driver);
}

static void __exit itmtouch_exit(void)
{
	usb_deregister(&itmtouch_driver);
}

module_init(itmtouch_init);
module_exit(itmtouch_exit);
