// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pegasus Mobile Notetaker Pen input tablet driver
 *
 * Copyright (c) 2016 Martin Kepplinger <martink@posteo.de>
 */

/*
 * request packet (control endpoint):
 * |-------------------------------------|
 * | Report ID | Nr of bytes | command   |
 * | (1 byte)  | (1 byte)    | (n bytes) |
 * |-------------------------------------|
 * | 0x02      | n           |           |
 * |-------------------------------------|
 *
 * data packet after set xy mode command, 0x80 0xb5 0x02 0x01
 * and pen is in range:
 *
 * byte	byte name		value (bits)
 * --------------------------------------------
 * 0	status			0 1 0 0 0 0 X X
 * 1	color			0 0 0 0 H 0 S T
 * 2	X low
 * 3	X high
 * 4	Y low
 * 5	Y high
 *
 * X X	battery state:
 *	no state reported	0x00
 *	battery low		0x01
 *	battery good		0x02
 *
 * H	Hovering
 * S	Switch 1 (pen button)
 * T	Tip
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/usb/input.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>

/* USB HID defines */
#define USB_REQ_GET_REPORT		0x01
#define USB_REQ_SET_REPORT		0x09

#define USB_VENDOR_ID_PEGASUSTECH	0x0e20
#define USB_DEVICE_ID_PEGASUS_NOTETAKER_EN100	0x0101

/* device specific defines */
#define NOTETAKER_REPORT_ID		0x02
#define NOTETAKER_SET_CMD		0x80
#define NOTETAKER_SET_MODE		0xb5

#define NOTETAKER_LED_MOUSE		0x02
#define PEN_MODE_XY			0x01

#define SPECIAL_COMMAND			0x80
#define BUTTON_PRESSED			0xb5
#define COMMAND_VERSION			0xa9

/* in xy data packet */
#define BATTERY_NO_REPORT		0x40
#define BATTERY_LOW			0x41
#define BATTERY_GOOD			0x42
#define PEN_BUTTON_PRESSED		BIT(1)
#define PEN_TIP				BIT(0)

struct pegasus {
	unsigned char *data;
	u8 data_len;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct usb_interface *intf;
	struct urb *irq;

	/* serialize access to open/suspend */
	struct mutex pm_mutex;
	bool is_open;

	char name[128];
	char phys[64];
	struct work_struct init;
};

static int pegasus_control_msg(struct pegasus *pegasus, u8 *data, int len)
{
	const int sizeof_buf = len + 2;
	int result;
	int error;
	u8 *cmd_buf;

	cmd_buf = kmalloc(sizeof_buf, GFP_KERNEL);
	if (!cmd_buf)
		return -ENOMEM;

	cmd_buf[0] = NOTETAKER_REPORT_ID;
	cmd_buf[1] = len;
	memcpy(cmd_buf + 2, data, len);

	result = usb_control_msg(pegasus->usbdev,
				 usb_sndctrlpipe(pegasus->usbdev, 0),
				 USB_REQ_SET_REPORT,
				 USB_TYPE_VENDOR | USB_DIR_OUT,
				 0, 0, cmd_buf, sizeof_buf,
				 USB_CTRL_SET_TIMEOUT);

	kfree(cmd_buf);

	if (unlikely(result != sizeof_buf)) {
		error = result < 0 ? result : -EIO;
		dev_err(&pegasus->usbdev->dev, "control msg error: %d\n",
			error);
		return error;
	}

	return 0;
}

static int pegasus_set_mode(struct pegasus *pegasus, u8 mode, u8 led)
{
	u8 cmd[] = { NOTETAKER_SET_CMD, NOTETAKER_SET_MODE, led, mode };

	return pegasus_control_msg(pegasus, cmd, sizeof(cmd));
}

static void pegasus_parse_packet(struct pegasus *pegasus)
{
	unsigned char *data = pegasus->data;
	struct input_dev *dev = pegasus->dev;
	u16 x, y;

	switch (data[0]) {
	case SPECIAL_COMMAND:
		/* device button pressed */
		if (data[1] == BUTTON_PRESSED)
			schedule_work(&pegasus->init);

		break;

	/* xy data */
	case BATTERY_LOW:
		dev_warn_once(&dev->dev, "Pen battery low\n");
		fallthrough;

	case BATTERY_NO_REPORT:
	case BATTERY_GOOD:
		x = le16_to_cpup((__le16 *)&data[2]);
		y = le16_to_cpup((__le16 *)&data[4]);

		/* pen-up event */
		if (x == 0 && y == 0)
			break;

		input_report_key(dev, BTN_TOUCH, data[1] & PEN_TIP);
		input_report_key(dev, BTN_RIGHT, data[1] & PEN_BUTTON_PRESSED);
		input_report_key(dev, BTN_TOOL_PEN, 1);
		input_report_abs(dev, ABS_X, (s16)x);
		input_report_abs(dev, ABS_Y, y);

		input_sync(dev);
		break;

	default:
		dev_warn_once(&pegasus->usbdev->dev,
			      "unknown answer from device\n");
	}
}

static void pegasus_irq(struct urb *urb)
{
	struct pegasus *pegasus = urb->context;
	struct usb_device *dev = pegasus->usbdev;
	int retval;

	switch (urb->status) {
	case 0:
		pegasus_parse_packet(pegasus);
		usb_mark_last_busy(pegasus->usbdev);
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		dev_err(&dev->dev, "%s - urb shutting down with status: %d",
			__func__, urb->status);
		return;

	default:
		dev_err(&dev->dev, "%s - nonzero urb status received: %d",
			__func__, urb->status);
		break;
	}

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&dev->dev, "%s - usb_submit_urb failed with result %d",
			__func__, retval);
}

static void pegasus_init(struct work_struct *work)
{
	struct pegasus *pegasus = container_of(work, struct pegasus, init);
	int error;

	error = pegasus_set_mode(pegasus, PEN_MODE_XY, NOTETAKER_LED_MOUSE);
	if (error)
		dev_err(&pegasus->usbdev->dev, "pegasus_set_mode error: %d\n",
			error);
}

static int pegasus_open(struct input_dev *dev)
{
	struct pegasus *pegasus = input_get_drvdata(dev);
	int error;

	error = usb_autopm_get_interface(pegasus->intf);
	if (error)
		return error;

	mutex_lock(&pegasus->pm_mutex);
	pegasus->irq->dev = pegasus->usbdev;
	if (usb_submit_urb(pegasus->irq, GFP_KERNEL)) {
		error = -EIO;
		goto err_autopm_put;
	}

	error = pegasus_set_mode(pegasus, PEN_MODE_XY, NOTETAKER_LED_MOUSE);
	if (error)
		goto err_kill_urb;

	pegasus->is_open = true;
	mutex_unlock(&pegasus->pm_mutex);
	return 0;

err_kill_urb:
	usb_kill_urb(pegasus->irq);
	cancel_work_sync(&pegasus->init);
err_autopm_put:
	mutex_unlock(&pegasus->pm_mutex);
	usb_autopm_put_interface(pegasus->intf);
	return error;
}

static void pegasus_close(struct input_dev *dev)
{
	struct pegasus *pegasus = input_get_drvdata(dev);

	mutex_lock(&pegasus->pm_mutex);
	usb_kill_urb(pegasus->irq);
	cancel_work_sync(&pegasus->init);
	pegasus->is_open = false;
	mutex_unlock(&pegasus->pm_mutex);

	usb_autopm_put_interface(pegasus->intf);
}

static int pegasus_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct pegasus *pegasus;
	struct input_dev *input_dev;
	int error;
	int pipe;

	/* We control interface 0 */
	if (intf->cur_altsetting->desc.bInterfaceNumber >= 1)
		return -ENODEV;

	/* Sanity check that the device has an endpoint */
	if (intf->cur_altsetting->desc.bNumEndpoints < 1) {
		dev_err(&intf->dev, "Invalid number of endpoints\n");
		return -EINVAL;
	}

	endpoint = &intf->cur_altsetting->endpoint[0].desc;

	pegasus = kzalloc(sizeof(*pegasus), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!pegasus || !input_dev) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	mutex_init(&pegasus->pm_mutex);

	pegasus->usbdev = dev;
	pegasus->dev = input_dev;
	pegasus->intf = intf;

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	pegasus->data_len = usb_maxpacket(dev, pipe);

	pegasus->data = usb_alloc_coherent(dev, pegasus->data_len, GFP_KERNEL,
					   &pegasus->data_dma);
	if (!pegasus->data) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	pegasus->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->irq) {
		error = -ENOMEM;
		goto err_free_dma;
	}

	usb_fill_int_urb(pegasus->irq, dev, pipe,
			 pegasus->data, pegasus->data_len,
			 pegasus_irq, pegasus, endpoint->bInterval);

	pegasus->irq->transfer_dma = pegasus->data_dma;
	pegasus->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	if (dev->manufacturer)
		strlcpy(pegasus->name, dev->manufacturer,
			sizeof(pegasus->name));

	if (dev->product) {
		if (dev->manufacturer)
			strlcat(pegasus->name, " ", sizeof(pegasus->name));
		strlcat(pegasus->name, dev->product, sizeof(pegasus->name));
	}

	if (!strlen(pegasus->name))
		snprintf(pegasus->name, sizeof(pegasus->name),
			 "USB Pegasus Device %04x:%04x",
			 le16_to_cpu(dev->descriptor.idVendor),
			 le16_to_cpu(dev->descriptor.idProduct));

	usb_make_path(dev, pegasus->phys, sizeof(pegasus->phys));
	strlcat(pegasus->phys, "/input0", sizeof(pegasus->phys));

	INIT_WORK(&pegasus->init, pegasus_init);

	usb_set_intfdata(intf, pegasus);

	input_dev->name = pegasus->name;
	input_dev->phys = pegasus->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, pegasus);

	input_dev->open = pegasus_open;
	input_dev->close = pegasus_close;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);

	__set_bit(ABS_X, input_dev->absbit);
	__set_bit(ABS_Y, input_dev->absbit);

	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(BTN_RIGHT, input_dev->keybit);
	__set_bit(BTN_TOOL_PEN, input_dev->keybit);

	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	__set_bit(INPUT_PROP_POINTER, input_dev->propbit);

	input_set_abs_params(input_dev, ABS_X, -1500, 1500, 8, 0);
	input_set_abs_params(input_dev, ABS_Y, 1600, 3000, 8, 0);

	error = input_register_device(pegasus->dev);
	if (error)
		goto err_free_urb;

	return 0;

err_free_urb:
	usb_free_urb(pegasus->irq);
err_free_dma:
	usb_free_coherent(dev, pegasus->data_len,
			  pegasus->data, pegasus->data_dma);
err_free_mem:
	input_free_device(input_dev);
	kfree(pegasus);
	usb_set_intfdata(intf, NULL);

	return error;
}

static void pegasus_disconnect(struct usb_interface *intf)
{
	struct pegasus *pegasus = usb_get_intfdata(intf);

	input_unregister_device(pegasus->dev);

	usb_free_urb(pegasus->irq);
	usb_free_coherent(interface_to_usbdev(intf),
			  pegasus->data_len, pegasus->data,
			  pegasus->data_dma);

	kfree(pegasus);
	usb_set_intfdata(intf, NULL);
}

static int pegasus_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct pegasus *pegasus = usb_get_intfdata(intf);

	mutex_lock(&pegasus->pm_mutex);
	usb_kill_urb(pegasus->irq);
	cancel_work_sync(&pegasus->init);
	mutex_unlock(&pegasus->pm_mutex);

	return 0;
}

static int pegasus_resume(struct usb_interface *intf)
{
	struct pegasus *pegasus = usb_get_intfdata(intf);
	int retval = 0;

	mutex_lock(&pegasus->pm_mutex);
	if (pegasus->is_open && usb_submit_urb(pegasus->irq, GFP_NOIO) < 0)
		retval = -EIO;
	mutex_unlock(&pegasus->pm_mutex);

	return retval;
}

static int pegasus_reset_resume(struct usb_interface *intf)
{
	struct pegasus *pegasus = usb_get_intfdata(intf);
	int retval = 0;

	mutex_lock(&pegasus->pm_mutex);
	if (pegasus->is_open) {
		retval = pegasus_set_mode(pegasus, PEN_MODE_XY,
					  NOTETAKER_LED_MOUSE);
		if (!retval && usb_submit_urb(pegasus->irq, GFP_NOIO) < 0)
			retval = -EIO;
	}
	mutex_unlock(&pegasus->pm_mutex);

	return retval;
}

static const struct usb_device_id pegasus_ids[] = {
	{ USB_DEVICE(USB_VENDOR_ID_PEGASUSTECH,
		     USB_DEVICE_ID_PEGASUS_NOTETAKER_EN100) },
	{ }
};
MODULE_DEVICE_TABLE(usb, pegasus_ids);

static struct usb_driver pegasus_driver = {
	.name		= "pegasus_notetaker",
	.probe		= pegasus_probe,
	.disconnect	= pegasus_disconnect,
	.suspend	= pegasus_suspend,
	.resume		= pegasus_resume,
	.reset_resume	= pegasus_reset_resume,
	.id_table	= pegasus_ids,
	.supports_autosuspend = 1,
};

module_usb_driver(pegasus_driver);

MODULE_AUTHOR("Martin Kepplinger <martink@posteo.de>");
MODULE_DESCRIPTION("Pegasus Mobile Notetaker Pen tablet driver");
MODULE_LICENSE("GPL");
