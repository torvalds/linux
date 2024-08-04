// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb/input.h>
#include <asm/unaligned.h>

/*
 * Pressure-threshold modules param code from Alex Perry <alex.perry@ieee.org>
 */

MODULE_AUTHOR("Josh Myer <josh@joshisanerd.com>");
MODULE_DESCRIPTION("USB KB Gear JamStudio Tablet driver");
MODULE_LICENSE("GPL");

#define USB_VENDOR_ID_KBGEAR	0x084e

static int kb_pressure_click = 0x10;
module_param(kb_pressure_click, int, 0);
MODULE_PARM_DESC(kb_pressure_click, "pressure threshold for clicks");

struct kbtab {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_interface *intf;
	struct urb *irq;
	char phys[32];
};

static void kbtab_irq(struct urb *urb)
{
	struct kbtab *kbtab = urb->context;
	unsigned char *data = kbtab->data;
	struct input_dev *dev = kbtab->dev;
	int pressure;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&kbtab->intf->dev,
			"%s - urb shutting down with status: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(&kbtab->intf->dev,
			"%s - nonzero urb status received: %d\n",
			__func__, urb->status);
		goto exit;
	}


	input_report_key(dev, BTN_TOOL_PEN, 1);

	input_report_abs(dev, ABS_X, get_unaligned_le16(&data[1]));
	input_report_abs(dev, ABS_Y, get_unaligned_le16(&data[3]));

	/*input_report_key(dev, BTN_TOUCH , data[0] & 0x01);*/
	input_report_key(dev, BTN_RIGHT, data[0] & 0x02);

	pressure = data[5];
	if (kb_pressure_click == -1)
		input_report_abs(dev, ABS_PRESSURE, pressure);
	else
		input_report_key(dev, BTN_LEFT, pressure > kb_pressure_click ? 1 : 0);

	input_sync(dev);

 exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&kbtab->intf->dev,
			"%s - usb_submit_urb failed with result %d\n",
			__func__, retval);
}

static const struct usb_device_id kbtab_ids[] = {
	{ USB_DEVICE(USB_VENDOR_ID_KBGEAR, 0x1001), .driver_info = 0 },
	{ }
};

MODULE_DEVICE_TABLE(usb, kbtab_ids);

static int kbtab_open(struct input_dev *dev)
{
	struct kbtab *kbtab = input_get_drvdata(dev);
	struct usb_device *udev = interface_to_usbdev(kbtab->intf);

	kbtab->irq->dev = udev;
	if (usb_submit_urb(kbtab->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void kbtab_close(struct input_dev *dev)
{
	struct kbtab *kbtab = input_get_drvdata(dev);

	usb_kill_urb(kbtab->irq);
}

static int kbtab_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct kbtab *kbtab;
	struct input_dev *input_dev;
	int error = -ENOMEM;

	if (intf->cur_altsetting->desc.bNumEndpoints < 1)
		return -ENODEV;

	endpoint = &intf->cur_altsetting->endpoint[0].desc;
	if (!usb_endpoint_is_int_in(endpoint))
		return -ENODEV;

	kbtab = kzalloc(sizeof(*kbtab), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!kbtab || !input_dev)
		goto fail1;

	kbtab->data = usb_alloc_coherent(dev, 8, GFP_KERNEL, &kbtab->data_dma);
	if (!kbtab->data)
		goto fail1;

	kbtab->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!kbtab->irq)
		goto fail2;

	kbtab->intf = intf;
	kbtab->dev = input_dev;

	usb_make_path(dev, kbtab->phys, sizeof(kbtab->phys));
	strlcat(kbtab->phys, "/input0", sizeof(kbtab->phys));

	input_dev->name = "KB Gear Tablet";
	input_dev->phys = kbtab->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, kbtab);

	input_dev->open = kbtab_open;
	input_dev->close = kbtab_close;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_LEFT)] |=
		BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |=
		BIT_MASK(BTN_TOOL_PEN) | BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, 0, 0x2000, 4, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 0x1750, 4, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 0xff, 0, 0);

	usb_fill_int_urb(kbtab->irq, dev,
			 usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			 kbtab->data, 8,
			 kbtab_irq, kbtab, endpoint->bInterval);
	kbtab->irq->transfer_dma = kbtab->data_dma;
	kbtab->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(kbtab->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, kbtab);

	return 0;

 fail3:	usb_free_urb(kbtab->irq);
 fail2:	usb_free_coherent(dev, 8, kbtab->data, kbtab->data_dma);
 fail1:	input_free_device(input_dev);
	kfree(kbtab);
	return error;
}

static void kbtab_disconnect(struct usb_interface *intf)
{
	struct kbtab *kbtab = usb_get_intfdata(intf);
	struct usb_device *udev = interface_to_usbdev(intf);

	usb_set_intfdata(intf, NULL);

	input_unregister_device(kbtab->dev);
	usb_free_urb(kbtab->irq);
	usb_free_coherent(udev, 8, kbtab->data, kbtab->data_dma);
	kfree(kbtab);
}

static struct usb_driver kbtab_driver = {
	.name =		"kbtab",
	.probe =	kbtab_probe,
	.disconnect =	kbtab_disconnect,
	.id_table =	kbtab_ids,
};

module_usb_driver(kbtab_driver);
