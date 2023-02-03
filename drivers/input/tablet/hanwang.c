// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  USB Hanwang tablet support
 *
 *  Copyright (c) 2010 Xing Wei <weixing@hanwang.com.cn>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb/input.h>

MODULE_AUTHOR("Xing Wei <weixing@hanwang.com.cn>");
MODULE_DESCRIPTION("USB Hanwang tablet driver");
MODULE_LICENSE("GPL");

#define USB_VENDOR_ID_HANWANG		0x0b57
#define HANWANG_TABLET_INT_CLASS	0x0003
#define HANWANG_TABLET_INT_SUB_CLASS	0x0001
#define HANWANG_TABLET_INT_PROTOCOL	0x0002

#define ART_MASTER_PKGLEN_MAX	10

/* device IDs */
#define STYLUS_DEVICE_ID	0x02
#define TOUCH_DEVICE_ID		0x03
#define CURSOR_DEVICE_ID	0x06
#define ERASER_DEVICE_ID	0x0A
#define PAD_DEVICE_ID		0x0F

/* match vendor and interface info  */
#define HANWANG_TABLET_DEVICE(vend, cl, sc, pr) \
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR \
		| USB_DEVICE_ID_MATCH_INT_INFO, \
	.idVendor = (vend), \
	.bInterfaceClass = (cl), \
	.bInterfaceSubClass = (sc), \
	.bInterfaceProtocol = (pr)

enum hanwang_tablet_type {
	HANWANG_ART_MASTER_III,
	HANWANG_ART_MASTER_HD,
	HANWANG_ART_MASTER_II,
};

struct hanwang {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct urb *irq;
	const struct hanwang_features *features;
	unsigned int current_tool;
	unsigned int current_id;
	char name[64];
	char phys[32];
};

struct hanwang_features {
	unsigned short pid;
	char *name;
	enum hanwang_tablet_type type;
	int pkg_len;
	int max_x;
	int max_y;
	int max_tilt_x;
	int max_tilt_y;
	int max_pressure;
};

static const struct hanwang_features features_array[] = {
	{ 0x8528, "Hanwang Art Master III 0906", HANWANG_ART_MASTER_III,
	  ART_MASTER_PKGLEN_MAX, 0x5757, 0x3692, 0x3f, 0x7f, 2048 },
	{ 0x8529, "Hanwang Art Master III 0604", HANWANG_ART_MASTER_III,
	  ART_MASTER_PKGLEN_MAX, 0x3d84, 0x2672, 0x3f, 0x7f, 2048 },
	{ 0x852a, "Hanwang Art Master III 1308", HANWANG_ART_MASTER_III,
	  ART_MASTER_PKGLEN_MAX, 0x7f00, 0x4f60, 0x3f, 0x7f, 2048 },
	{ 0x8401, "Hanwang Art Master HD 5012", HANWANG_ART_MASTER_HD,
	  ART_MASTER_PKGLEN_MAX, 0x678e, 0x4150, 0x3f, 0x7f, 1024 },
	{ 0x8503, "Hanwang Art Master II", HANWANG_ART_MASTER_II,
	  ART_MASTER_PKGLEN_MAX, 0x27de, 0x1cfe, 0x3f, 0x7f, 1024 },
};

static const int hw_eventtypes[] = {
	EV_KEY, EV_ABS, EV_MSC,
};

static const int hw_absevents[] = {
	ABS_X, ABS_Y, ABS_TILT_X, ABS_TILT_Y, ABS_WHEEL,
	ABS_RX, ABS_RY, ABS_PRESSURE, ABS_MISC,
};

static const int hw_btnevents[] = {
	BTN_STYLUS, BTN_STYLUS2, BTN_TOOL_PEN, BTN_TOOL_RUBBER,
	BTN_TOOL_MOUSE, BTN_TOOL_FINGER,
	BTN_0, BTN_1, BTN_2, BTN_3, BTN_4, BTN_5, BTN_6, BTN_7, BTN_8,
};

static const int hw_mscevents[] = {
	MSC_SERIAL,
};

static void hanwang_parse_packet(struct hanwang *hanwang)
{
	unsigned char *data = hanwang->data;
	struct input_dev *input_dev = hanwang->dev;
	struct usb_device *dev = hanwang->usbdev;
	enum hanwang_tablet_type type = hanwang->features->type;
	int i;
	u16 p;

	if (type == HANWANG_ART_MASTER_II) {
		hanwang->current_tool = BTN_TOOL_PEN;
		hanwang->current_id = STYLUS_DEVICE_ID;
	}

	switch (data[0]) {
	case 0x02:	/* data packet */
		switch (data[1]) {
		case 0x80:	/* tool prox out */
			if (type != HANWANG_ART_MASTER_II) {
				hanwang->current_id = 0;
				input_report_key(input_dev,
						 hanwang->current_tool, 0);
			}
			break;

		case 0x00:	/* artmaster ii pen leave */
			if (type == HANWANG_ART_MASTER_II) {
				hanwang->current_id = 0;
				input_report_key(input_dev,
						 hanwang->current_tool, 0);
			}
			break;

		case 0xc2:	/* first time tool prox in */
			switch (data[3] & 0xf0) {
			case 0x20:	/* art_master III */
			case 0x30:	/* art_master_HD */
				hanwang->current_id = STYLUS_DEVICE_ID;
				hanwang->current_tool = BTN_TOOL_PEN;
				input_report_key(input_dev, BTN_TOOL_PEN, 1);
				break;
			case 0xa0:	/* art_master III */
			case 0xb0:	/* art_master_HD */
				hanwang->current_id = ERASER_DEVICE_ID;
				hanwang->current_tool = BTN_TOOL_RUBBER;
				input_report_key(input_dev, BTN_TOOL_RUBBER, 1);
				break;
			default:
				hanwang->current_id = 0;
				dev_dbg(&dev->dev,
					"unknown tablet tool %02x\n", data[0]);
				break;
			}
			break;

		default:	/* tool data packet */
			switch (type) {
			case HANWANG_ART_MASTER_III:
				p = (data[6] << 3) |
				    ((data[7] & 0xc0) >> 5) |
				    (data[1] & 0x01);
				break;

			case HANWANG_ART_MASTER_HD:
			case HANWANG_ART_MASTER_II:
				p = (data[7] >> 6) | (data[6] << 2);
				break;

			default:
				p = 0;
				break;
			}

			input_report_abs(input_dev, ABS_X,
					 be16_to_cpup((__be16 *)&data[2]));
			input_report_abs(input_dev, ABS_Y,
					 be16_to_cpup((__be16 *)&data[4]));
			input_report_abs(input_dev, ABS_PRESSURE, p);
			input_report_abs(input_dev, ABS_TILT_X, data[7] & 0x3f);
			input_report_abs(input_dev, ABS_TILT_Y, data[8] & 0x7f);
			input_report_key(input_dev, BTN_STYLUS, data[1] & 0x02);

			if (type != HANWANG_ART_MASTER_II)
				input_report_key(input_dev, BTN_STYLUS2,
						 data[1] & 0x04);
			else
				input_report_key(input_dev, BTN_TOOL_PEN, 1);

			break;
		}

		input_report_abs(input_dev, ABS_MISC, hanwang->current_id);
		input_event(input_dev, EV_MSC, MSC_SERIAL,
				hanwang->features->pid);
		break;

	case 0x0c:
		/* roll wheel */
		hanwang->current_id = PAD_DEVICE_ID;

		switch (type) {
		case HANWANG_ART_MASTER_III:
			input_report_key(input_dev, BTN_TOOL_FINGER,
					 data[1] || data[2] || data[3]);
			input_report_abs(input_dev, ABS_WHEEL, data[1]);
			input_report_key(input_dev, BTN_0, data[2]);
			for (i = 0; i < 8; i++)
				input_report_key(input_dev,
					 BTN_1 + i, data[3] & (1 << i));
			break;

		case HANWANG_ART_MASTER_HD:
			input_report_key(input_dev, BTN_TOOL_FINGER, data[1] ||
					data[2] || data[3] || data[4] ||
					data[5] || data[6]);
			input_report_abs(input_dev, ABS_RX,
					((data[1] & 0x1f) << 8) | data[2]);
			input_report_abs(input_dev, ABS_RY,
					((data[3] & 0x1f) << 8) | data[4]);
			input_report_key(input_dev, BTN_0, data[5] & 0x01);
			for (i = 0; i < 4; i++) {
				input_report_key(input_dev,
					 BTN_1 + i, data[5] & (1 << i));
				input_report_key(input_dev,
					 BTN_5 + i, data[6] & (1 << i));
			}
			break;

		case HANWANG_ART_MASTER_II:
			dev_dbg(&dev->dev, "error packet  %02x\n", data[0]);
			return;
		}

		input_report_abs(input_dev, ABS_MISC, hanwang->current_id);
		input_event(input_dev, EV_MSC, MSC_SERIAL, 0xffffffff);
		break;

	default:
		dev_dbg(&dev->dev, "error packet  %02x\n", data[0]);
		break;
	}

	input_sync(input_dev);
}

static void hanwang_irq(struct urb *urb)
{
	struct hanwang *hanwang = urb->context;
	struct usb_device *dev = hanwang->usbdev;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */;
		hanwang_parse_packet(hanwang);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
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

static int hanwang_open(struct input_dev *dev)
{
	struct hanwang *hanwang = input_get_drvdata(dev);

	hanwang->irq->dev = hanwang->usbdev;
	if (usb_submit_urb(hanwang->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void hanwang_close(struct input_dev *dev)
{
	struct hanwang *hanwang = input_get_drvdata(dev);

	usb_kill_urb(hanwang->irq);
}

static bool get_features(struct usb_device *dev, struct hanwang *hanwang)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(features_array); i++) {
		if (le16_to_cpu(dev->descriptor.idProduct) ==
				features_array[i].pid) {
			hanwang->features = &features_array[i];
			return true;
		}
	}

	return false;
}


static int hanwang_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct hanwang *hanwang;
	struct input_dev *input_dev;
	int error;
	int i;

	if (intf->cur_altsetting->desc.bNumEndpoints < 1)
		return -ENODEV;

	hanwang = kzalloc(sizeof(struct hanwang), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!hanwang || !input_dev) {
		error = -ENOMEM;
		goto fail1;
	}

	if (!get_features(dev, hanwang)) {
		error = -ENXIO;
		goto fail1;
	}

	hanwang->data = usb_alloc_coherent(dev, hanwang->features->pkg_len,
					GFP_KERNEL, &hanwang->data_dma);
	if (!hanwang->data) {
		error = -ENOMEM;
		goto fail1;
	}

	hanwang->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!hanwang->irq) {
		error = -ENOMEM;
		goto fail2;
	}

	hanwang->usbdev = dev;
	hanwang->dev = input_dev;

	usb_make_path(dev, hanwang->phys, sizeof(hanwang->phys));
	strlcat(hanwang->phys, "/input0", sizeof(hanwang->phys));

	strscpy(hanwang->name, hanwang->features->name, sizeof(hanwang->name));
	input_dev->name = hanwang->name;
	input_dev->phys = hanwang->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, hanwang);

	input_dev->open = hanwang_open;
	input_dev->close = hanwang_close;

	for (i = 0; i < ARRAY_SIZE(hw_eventtypes); ++i)
		__set_bit(hw_eventtypes[i], input_dev->evbit);

	for (i = 0; i < ARRAY_SIZE(hw_absevents); ++i)
		__set_bit(hw_absevents[i], input_dev->absbit);

	for (i = 0; i < ARRAY_SIZE(hw_btnevents); ++i)
		__set_bit(hw_btnevents[i], input_dev->keybit);

	for (i = 0; i < ARRAY_SIZE(hw_mscevents); ++i)
		__set_bit(hw_mscevents[i], input_dev->mscbit);

	input_set_abs_params(input_dev, ABS_X,
			     0, hanwang->features->max_x, 4, 0);
	input_set_abs_params(input_dev, ABS_Y,
			     0, hanwang->features->max_y, 4, 0);
	input_set_abs_params(input_dev, ABS_TILT_X,
			     0, hanwang->features->max_tilt_x, 0, 0);
	input_set_abs_params(input_dev, ABS_TILT_Y,
			     0, hanwang->features->max_tilt_y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE,
			     0, hanwang->features->max_pressure, 0, 0);

	endpoint = &intf->cur_altsetting->endpoint[0].desc;
	usb_fill_int_urb(hanwang->irq, dev,
			usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			hanwang->data, hanwang->features->pkg_len,
			hanwang_irq, hanwang, endpoint->bInterval);
	hanwang->irq->transfer_dma = hanwang->data_dma;
	hanwang->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(hanwang->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, hanwang);

	return 0;

 fail3:	usb_free_urb(hanwang->irq);
 fail2:	usb_free_coherent(dev, hanwang->features->pkg_len,
			hanwang->data, hanwang->data_dma);
 fail1:	input_free_device(input_dev);
	kfree(hanwang);
	return error;

}

static void hanwang_disconnect(struct usb_interface *intf)
{
	struct hanwang *hanwang = usb_get_intfdata(intf);

	input_unregister_device(hanwang->dev);
	usb_free_urb(hanwang->irq);
	usb_free_coherent(interface_to_usbdev(intf),
			hanwang->features->pkg_len, hanwang->data,
			hanwang->data_dma);
	kfree(hanwang);
	usb_set_intfdata(intf, NULL);
}

static const struct usb_device_id hanwang_ids[] = {
	{ HANWANG_TABLET_DEVICE(USB_VENDOR_ID_HANWANG, HANWANG_TABLET_INT_CLASS,
		HANWANG_TABLET_INT_SUB_CLASS, HANWANG_TABLET_INT_PROTOCOL) },
	{}
};

MODULE_DEVICE_TABLE(usb, hanwang_ids);

static struct usb_driver hanwang_driver = {
	.name		= "hanwang",
	.probe		= hanwang_probe,
	.disconnect	= hanwang_disconnect,
	.id_table	= hanwang_ids,
};

module_usb_driver(hanwang_driver);
