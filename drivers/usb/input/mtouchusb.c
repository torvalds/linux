/******************************************************************************
 * mtouchusb.c  --  Driver for Microtouch (Now 3M) USB Touchscreens
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
 * Based upon original work by Radoslaw Garbacz (usb-support@ite.pl)
 *  (http://freshmeat.net/projects/3mtouchscreendriver)
 *
 * History
 *
 *  0.3 & 0.4  2002 (TEJ) tejohnson@yahoo.com
 *    Updated to 2.4.18, then 2.4.19
 *    Old version still relied on stealing a minor
 *
 *  0.5  02/26/2004 (TEJ) tejohnson@yahoo.com
 *    Complete rewrite using Linux Input in 2.6.3
 *    Unfortunately no calibration support at this time
 *
 *  1.4 04/25/2004 (TEJ) tejohnson@yahoo.com
 *    Changed reset from standard USB dev reset to vendor reset
 *    Changed data sent to host from compensated to raw coordinates
 *    Eliminated vendor/product module params
 *    Performed multiple successful tests with an EXII-5010UC
 *
 *  1.5 02/27/2005 ddstreet@ieee.org
 *    Added module parameter to select raw or hw-calibrated coordinate reporting
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>

#define MTOUCHUSB_MIN_XC                0x0
#define MTOUCHUSB_MAX_RAW_XC            0x4000
#define MTOUCHUSB_MAX_CALIB_XC          0xffff
#define MTOUCHUSB_XC_FUZZ               0x0
#define MTOUCHUSB_XC_FLAT               0x0
#define MTOUCHUSB_MIN_YC                0x0
#define MTOUCHUSB_MAX_RAW_YC            0x4000
#define MTOUCHUSB_MAX_CALIB_YC          0xffff
#define MTOUCHUSB_YC_FUZZ               0x0
#define MTOUCHUSB_YC_FLAT               0x0

#define MTOUCHUSB_ASYNC_REPORT          1
#define MTOUCHUSB_RESET                 7
#define MTOUCHUSB_REPORT_DATA_SIZE      11
#define MTOUCHUSB_REQ_CTRLLR_ID         10

#define MTOUCHUSB_GET_RAW_XC(data)      (data[8]<<8 | data[7])
#define MTOUCHUSB_GET_CALIB_XC(data)    (data[4]<<8 | data[3])
#define MTOUCHUSB_GET_RAW_YC(data)      (data[10]<<8 | data[9])
#define MTOUCHUSB_GET_CALIB_YC(data)    (data[6]<<8 | data[5])
#define MTOUCHUSB_GET_XC(data)          (raw_coordinates ? \
                                         MTOUCHUSB_GET_RAW_XC(data) : \
                                         MTOUCHUSB_GET_CALIB_XC(data))
#define MTOUCHUSB_GET_YC(data)          (raw_coordinates ? \
                                         MTOUCHUSB_GET_RAW_YC(data) : \
                                         MTOUCHUSB_GET_CALIB_YC(data))
#define MTOUCHUSB_GET_TOUCHED(data)     ((data[2] & 0x40) ? 1:0)

#define DRIVER_VERSION "v1.5"
#define DRIVER_AUTHOR "Todd E. Johnson, tejohnson@yahoo.com"
#define DRIVER_DESC "3M USB Touchscreen Driver"
#define DRIVER_LICENSE "GPL"

static int raw_coordinates = 1;

module_param(raw_coordinates, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(raw_coordinates, "report raw coordinate values (y, default) or hardware-calibrated coordinate values (n)");

struct mtouch_usb {
	unsigned char *data;
	dma_addr_t data_dma;
	struct urb *irq;
	struct usb_device *udev;
	struct input_dev *input;
	char name[128];
	char phys[64];
};

static struct usb_device_id mtouchusb_devices[] = {
	{ USB_DEVICE(0x0596, 0x0001) },
	{ }
};

static void mtouchusb_irq(struct urb *urb)
{
	struct mtouch_usb *mtouch = urb->context;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ETIME:
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

	input_report_key(mtouch->input, BTN_TOUCH,
			 MTOUCHUSB_GET_TOUCHED(mtouch->data));
	input_report_abs(mtouch->input, ABS_X, MTOUCHUSB_GET_XC(mtouch->data));
	input_report_abs(mtouch->input, ABS_Y,
			 (raw_coordinates ? MTOUCHUSB_MAX_RAW_YC : MTOUCHUSB_MAX_CALIB_YC)
			 - MTOUCHUSB_GET_YC(mtouch->data));
	input_sync(mtouch->input);

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		err("%s - usb_submit_urb failed with result: %d",
		    __FUNCTION__, retval);
}

static int mtouchusb_open(struct input_dev *input)
{
	struct mtouch_usb *mtouch = input->private;

	mtouch->irq->dev = mtouch->udev;

	if (usb_submit_urb(mtouch->irq, GFP_ATOMIC))
		return -EIO;

	return 0;
}

static void mtouchusb_close(struct input_dev *input)
{
	struct mtouch_usb *mtouch = input->private;

	usb_kill_urb(mtouch->irq);
}

static int mtouchusb_alloc_buffers(struct usb_device *udev, struct mtouch_usb *mtouch)
{
	dbg("%s - called", __FUNCTION__);

	mtouch->data = usb_buffer_alloc(udev, MTOUCHUSB_REPORT_DATA_SIZE,
					GFP_ATOMIC, &mtouch->data_dma);

	if (!mtouch->data)
		return -1;

	return 0;
}

static void mtouchusb_free_buffers(struct usb_device *udev, struct mtouch_usb *mtouch)
{
	dbg("%s - called", __FUNCTION__);

	if (mtouch->data)
		usb_buffer_free(udev, MTOUCHUSB_REPORT_DATA_SIZE,
				mtouch->data, mtouch->data_dma);
}

static int mtouchusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct mtouch_usb *mtouch;
	struct input_dev *input_dev;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *udev = interface_to_usbdev(intf);
	int nRet;

	dbg("%s - called", __FUNCTION__);

	dbg("%s - setting interface", __FUNCTION__);
	interface = intf->cur_altsetting;

	dbg("%s - setting endpoint", __FUNCTION__);
	endpoint = &interface->endpoint[0].desc;

	mtouch = kzalloc(sizeof(struct mtouch_usb), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!mtouch || !input_dev) {
		err("%s - Out of memory.", __FUNCTION__);
		goto fail1;
	}

	dbg("%s - allocating buffers", __FUNCTION__);
	if (mtouchusb_alloc_buffers(udev, mtouch))
		goto fail2;

	mtouch->udev = udev;
	mtouch->input = input_dev;

	if (udev->manufacturer)
		strlcpy(mtouch->name, udev->manufacturer, sizeof(mtouch->name));

	if (udev->product) {
		if (udev->manufacturer)
			strlcat(mtouch->name, " ", sizeof(mtouch->name));
		strlcat(mtouch->name, udev->product, sizeof(mtouch->name));
	}

	if (!strlen(mtouch->name))
		snprintf(mtouch->name, sizeof(mtouch->name),
			"USB Touchscreen %04x:%04x",
			le16_to_cpu(udev->descriptor.idVendor),
			le16_to_cpu(udev->descriptor.idProduct));

	usb_make_path(udev, mtouch->phys, sizeof(mtouch->phys));
	strlcpy(mtouch->phys, "/input0", sizeof(mtouch->phys));

	input_dev->name = mtouch->name;
	input_dev->phys = mtouch->phys;
	usb_to_input_id(udev, &input_dev->id);
	input_dev->cdev.dev = &intf->dev;
	input_dev->private = mtouch;

	input_dev->open = mtouchusb_open;
	input_dev->close = mtouchusb_close;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	input_dev->keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, MTOUCHUSB_MIN_XC,
		raw_coordinates ? MTOUCHUSB_MAX_RAW_XC : MTOUCHUSB_MAX_CALIB_XC,
				MTOUCHUSB_XC_FUZZ, MTOUCHUSB_XC_FLAT);
	input_set_abs_params(input_dev, ABS_Y, MTOUCHUSB_MIN_YC,
		raw_coordinates ? MTOUCHUSB_MAX_RAW_YC : MTOUCHUSB_MAX_CALIB_YC,
		MTOUCHUSB_YC_FUZZ, MTOUCHUSB_YC_FLAT);

	nRet = usb_control_msg(mtouch->udev, usb_rcvctrlpipe(udev, 0),
			       MTOUCHUSB_RESET,
			       USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			       1, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
	dbg("%s - usb_control_msg - MTOUCHUSB_RESET - bytes|err: %d",
	    __FUNCTION__, nRet);

	dbg("%s - usb_alloc_urb: mtouch->irq", __FUNCTION__);
	mtouch->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!mtouch->irq) {
		dbg("%s - usb_alloc_urb failed: mtouch->irq", __FUNCTION__);
		goto fail2;
	}

	dbg("%s - usb_fill_int_urb", __FUNCTION__);
	usb_fill_int_urb(mtouch->irq, mtouch->udev,
			 usb_rcvintpipe(mtouch->udev, 0x81),
			 mtouch->data, MTOUCHUSB_REPORT_DATA_SIZE,
			 mtouchusb_irq, mtouch, endpoint->bInterval);

	dbg("%s - input_register_device", __FUNCTION__);
	input_register_device(mtouch->input);

	nRet = usb_control_msg(mtouch->udev, usb_rcvctrlpipe(udev, 0),
			       MTOUCHUSB_ASYNC_REPORT,
			       USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			       1, 1, NULL, 0, USB_CTRL_SET_TIMEOUT);
	dbg("%s - usb_control_msg - MTOUCHUSB_ASYNC_REPORT - bytes|err: %d",
	    __FUNCTION__, nRet);

	usb_set_intfdata(intf, mtouch);
	return 0;

fail2:	mtouchusb_free_buffers(udev, mtouch);
fail1:	input_free_device(input_dev);
	kfree(mtouch);
	return -ENOMEM;
}

static void mtouchusb_disconnect(struct usb_interface *intf)
{
	struct mtouch_usb *mtouch = usb_get_intfdata(intf);

	dbg("%s - called", __FUNCTION__);
	usb_set_intfdata(intf, NULL);
	if (mtouch) {
		dbg("%s - mtouch is initialized, cleaning up", __FUNCTION__);
		usb_kill_urb(mtouch->irq);
		input_unregister_device(mtouch->input);
		usb_free_urb(mtouch->irq);
		mtouchusb_free_buffers(interface_to_usbdev(intf), mtouch);
		kfree(mtouch);
	}
}

MODULE_DEVICE_TABLE(usb, mtouchusb_devices);

static struct usb_driver mtouchusb_driver = {
	.name		= "mtouchusb",
	.probe		= mtouchusb_probe,
	.disconnect	= mtouchusb_disconnect,
	.id_table	= mtouchusb_devices,
};

static int __init mtouchusb_init(void)
{
	dbg("%s - called", __FUNCTION__);
	return usb_register(&mtouchusb_driver);
}

static void __exit mtouchusb_cleanup(void)
{
	dbg("%s - called", __FUNCTION__);
	usb_deregister(&mtouchusb_driver);
}

module_init(mtouchusb_init);
module_exit(mtouchusb_cleanup);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
