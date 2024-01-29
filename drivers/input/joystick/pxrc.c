// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Phoenix RC Flight Controller Adapter
 *
 * Copyright (C) 2018 Marcus Folkesson <marcus.folkesson@gmail.com>
 */

#include <linux/cleanup.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/usb.h>
#include <linux/usb/input.h>

#define PXRC_VENDOR_ID		0x1781
#define PXRC_PRODUCT_ID		0x0898

struct pxrc {
	struct input_dev	*input;
	struct usb_interface	*intf;
	struct urb		*urb;
	struct mutex		pm_mutex;
	bool			is_open;
	char			phys[64];
};

static void pxrc_usb_irq(struct urb *urb)
{
	struct pxrc *pxrc = urb->context;
	u8 *data = urb->transfer_buffer;
	int error;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ETIME:
		/* this urb is timing out */
		dev_dbg(&pxrc->intf->dev,
			"%s - urb timed out - was the device unplugged?\n",
			__func__);
		return;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -EPIPE:
		/* this urb is terminated, clean up */
		dev_dbg(&pxrc->intf->dev, "%s - urb shutting down with status: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(&pxrc->intf->dev, "%s - nonzero urb status received: %d\n",
			__func__, urb->status);
		goto exit;
	}

	if (urb->actual_length == 8) {
		input_report_abs(pxrc->input, ABS_X, data[0]);
		input_report_abs(pxrc->input, ABS_Y, data[2]);
		input_report_abs(pxrc->input, ABS_RX, data[3]);
		input_report_abs(pxrc->input, ABS_RY, data[4]);
		input_report_abs(pxrc->input, ABS_RUDDER, data[5]);
		input_report_abs(pxrc->input, ABS_THROTTLE, data[6]);
		input_report_abs(pxrc->input, ABS_MISC, data[7]);

		input_report_key(pxrc->input, BTN_A, data[1]);
	}

exit:
	/* Resubmit to fetch new fresh URBs */
	error = usb_submit_urb(urb, GFP_ATOMIC);
	if (error && error != -EPERM)
		dev_err(&pxrc->intf->dev,
			"%s - usb_submit_urb failed with result: %d",
			__func__, error);
}

static int pxrc_open(struct input_dev *input)
{
	struct pxrc *pxrc = input_get_drvdata(input);
	int error;

	guard(mutex)(&pxrc->pm_mutex);
	error = usb_submit_urb(pxrc->urb, GFP_KERNEL);
	if (error) {
		dev_err(&pxrc->intf->dev,
			"%s - usb_submit_urb failed, error: %d\n",
			__func__, error);
		return -EIO;
	}

	pxrc->is_open = true;
	return 0;
}

static void pxrc_close(struct input_dev *input)
{
	struct pxrc *pxrc = input_get_drvdata(input);

	guard(mutex)(&pxrc->pm_mutex);
	usb_kill_urb(pxrc->urb);
	pxrc->is_open = false;
}

static void pxrc_free_urb(void *_pxrc)
{
	struct pxrc *pxrc = _pxrc;

	usb_free_urb(pxrc->urb);
}

static int pxrc_probe(struct usb_interface *intf,
		      const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct pxrc *pxrc;
	struct usb_endpoint_descriptor *epirq;
	size_t xfer_size;
	void *xfer_buf;
	int error;

	/*
	 * Locate the endpoint information. This device only has an
	 * interrupt endpoint.
	 */
	error = usb_find_common_endpoints(intf->cur_altsetting,
					  NULL, NULL, &epirq, NULL);
	if (error) {
		dev_err(&intf->dev, "Could not find endpoint\n");
		return error;
	}

	pxrc = devm_kzalloc(&intf->dev, sizeof(*pxrc), GFP_KERNEL);
	if (!pxrc)
		return -ENOMEM;

	mutex_init(&pxrc->pm_mutex);
	pxrc->intf = intf;

	usb_set_intfdata(pxrc->intf, pxrc);

	xfer_size = usb_endpoint_maxp(epirq);
	xfer_buf = devm_kmalloc(&intf->dev, xfer_size, GFP_KERNEL);
	if (!xfer_buf)
		return -ENOMEM;

	pxrc->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pxrc->urb)
		return -ENOMEM;

	error = devm_add_action_or_reset(&intf->dev, pxrc_free_urb, pxrc);
	if (error)
		return error;

	usb_fill_int_urb(pxrc->urb, udev,
			 usb_rcvintpipe(udev, epirq->bEndpointAddress),
			 xfer_buf, xfer_size, pxrc_usb_irq, pxrc, 1);

	pxrc->input = devm_input_allocate_device(&intf->dev);
	if (!pxrc->input) {
		dev_err(&intf->dev, "couldn't allocate input device\n");
		return -ENOMEM;
	}

	pxrc->input->name = "PXRC Flight Controller Adapter";

	usb_make_path(udev, pxrc->phys, sizeof(pxrc->phys));
	strlcat(pxrc->phys, "/input0", sizeof(pxrc->phys));
	pxrc->input->phys = pxrc->phys;

	usb_to_input_id(udev, &pxrc->input->id);

	pxrc->input->open = pxrc_open;
	pxrc->input->close = pxrc_close;

	input_set_capability(pxrc->input, EV_KEY, BTN_A);
	input_set_abs_params(pxrc->input, ABS_X, 0, 255, 0, 0);
	input_set_abs_params(pxrc->input, ABS_Y, 0, 255, 0, 0);
	input_set_abs_params(pxrc->input, ABS_RX, 0, 255, 0, 0);
	input_set_abs_params(pxrc->input, ABS_RY, 0, 255, 0, 0);
	input_set_abs_params(pxrc->input, ABS_RUDDER, 0, 255, 0, 0);
	input_set_abs_params(pxrc->input, ABS_THROTTLE, 0, 255, 0, 0);
	input_set_abs_params(pxrc->input, ABS_MISC, 0, 255, 0, 0);

	input_set_drvdata(pxrc->input, pxrc);

	error = input_register_device(pxrc->input);
	if (error)
		return error;

	return 0;
}

static void pxrc_disconnect(struct usb_interface *intf)
{
	/* All driver resources are devm-managed. */
}

static int pxrc_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct pxrc *pxrc = usb_get_intfdata(intf);

	guard(mutex)(&pxrc->pm_mutex);
	if (pxrc->is_open)
		usb_kill_urb(pxrc->urb);

	return 0;
}

static int pxrc_resume(struct usb_interface *intf)
{
	struct pxrc *pxrc = usb_get_intfdata(intf);

	guard(mutex)(&pxrc->pm_mutex);
	if (pxrc->is_open && usb_submit_urb(pxrc->urb, GFP_KERNEL) < 0)
		return -EIO;

	return 0;
}

static int pxrc_pre_reset(struct usb_interface *intf)
{
	struct pxrc *pxrc = usb_get_intfdata(intf);

	mutex_lock(&pxrc->pm_mutex);
	usb_kill_urb(pxrc->urb);
	return 0;
}

static int pxrc_post_reset(struct usb_interface *intf)
{
	struct pxrc *pxrc = usb_get_intfdata(intf);
	int retval = 0;

	if (pxrc->is_open && usb_submit_urb(pxrc->urb, GFP_KERNEL) < 0)
		retval = -EIO;

	mutex_unlock(&pxrc->pm_mutex);

	return retval;
}

static int pxrc_reset_resume(struct usb_interface *intf)
{
	return pxrc_resume(intf);
}

static const struct usb_device_id pxrc_table[] = {
	{ USB_DEVICE(PXRC_VENDOR_ID, PXRC_PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE(usb, pxrc_table);

static struct usb_driver pxrc_driver = {
	.name =		"pxrc",
	.probe =	pxrc_probe,
	.disconnect =	pxrc_disconnect,
	.id_table =	pxrc_table,
	.suspend	= pxrc_suspend,
	.resume		= pxrc_resume,
	.pre_reset	= pxrc_pre_reset,
	.post_reset	= pxrc_post_reset,
	.reset_resume	= pxrc_reset_resume,
};

module_usb_driver(pxrc_driver);

MODULE_AUTHOR("Marcus Folkesson <marcus.folkesson@gmail.com>");
MODULE_DESCRIPTION("PhoenixRC Flight Controller Adapter");
MODULE_LICENSE("GPL v2");
