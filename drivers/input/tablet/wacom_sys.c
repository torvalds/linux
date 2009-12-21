/*
 * drivers/input/tablet/wacom_sys.c
 *
 *  USB Wacom tablet support - system specific code
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "wacom.h"
#include "wacom_wac.h"

/* defines to get HID report descriptor */
#define HID_DEVICET_HID		(USB_TYPE_CLASS | 0x01)
#define HID_DEVICET_REPORT	(USB_TYPE_CLASS | 0x02)
#define HID_USAGE_UNDEFINED		0x00
#define HID_USAGE_PAGE			0x05
#define HID_USAGE_PAGE_DIGITIZER	0x0d
#define HID_USAGE_PAGE_DESKTOP		0x01
#define HID_USAGE			0x09
#define HID_USAGE_X			0x30
#define HID_USAGE_Y			0x31
#define HID_USAGE_X_TILT		0x3d
#define HID_USAGE_Y_TILT		0x3e
#define HID_USAGE_FINGER		0x22
#define HID_USAGE_STYLUS		0x20
#define HID_COLLECTION			0xc0

enum {
	WCM_UNDEFINED = 0,
	WCM_DESKTOP,
	WCM_DIGITIZER,
};

struct hid_descriptor {
	struct usb_descriptor_header header;
	__le16   bcdHID;
	u8       bCountryCode;
	u8       bNumDescriptors;
	u8       bDescriptorType;
	__le16   wDescriptorLength;
} __attribute__ ((packed));

/* defines to get/set USB message */
#define USB_REQ_GET_REPORT	0x01
#define USB_REQ_SET_REPORT	0x09
#define WAC_HID_FEATURE_REPORT	0x03

static int usb_get_report(struct usb_interface *intf, unsigned char type,
				unsigned char id, void *buf, int size)
{
	return usb_control_msg(interface_to_usbdev(intf),
		usb_rcvctrlpipe(interface_to_usbdev(intf), 0),
		USB_REQ_GET_REPORT, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		(type << 8) + id, intf->altsetting[0].desc.bInterfaceNumber,
		buf, size, 100);
}

static int usb_set_report(struct usb_interface *intf, unsigned char type,
				unsigned char id, void *buf, int size)
{
	return usb_control_msg(interface_to_usbdev(intf),
		usb_sndctrlpipe(interface_to_usbdev(intf), 0),
                USB_REQ_SET_REPORT, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                (type << 8) + id, intf->altsetting[0].desc.bInterfaceNumber,
		buf, size, 1000);
}

static struct input_dev * get_input_dev(struct wacom_combo *wcombo)
{
	return wcombo->wacom->dev;
}

static void wacom_sys_irq(struct urb *urb)
{
	struct wacom *wacom = urb->context;
	struct wacom_combo wcombo;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __func__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __func__, urb->status);
		goto exit;
	}

	wcombo.wacom = wacom;
	wcombo.urb = urb;

	if (wacom_wac_irq(wacom->wacom_wac, (void *)&wcombo))
		input_sync(get_input_dev(&wcombo));

 exit:
	usb_mark_last_busy(wacom->usbdev);
	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		err ("%s - usb_submit_urb failed with result %d",
		     __func__, retval);
}

void wacom_report_key(void *wcombo, unsigned int key_type, int key_data)
{
	input_report_key(get_input_dev((struct wacom_combo *)wcombo), key_type, key_data);
}

void wacom_report_abs(void *wcombo, unsigned int abs_type, int abs_data)
{
	input_report_abs(get_input_dev((struct wacom_combo *)wcombo), abs_type, abs_data);
}

void wacom_report_rel(void *wcombo, unsigned int rel_type, int rel_data)
{
	input_report_rel(get_input_dev((struct wacom_combo *)wcombo), rel_type, rel_data);
}

void wacom_input_event(void *wcombo, unsigned int type, unsigned int code, int value)
{
	input_event(get_input_dev((struct wacom_combo *)wcombo), type, code, value);
}

__u16 wacom_be16_to_cpu(unsigned char *data)
{
	__u16 value;
	value = be16_to_cpu(*(__be16 *) data);
	return value;
}

__u16 wacom_le16_to_cpu(unsigned char *data)
{
	__u16 value;
	value = le16_to_cpu(*(__le16 *) data);
	return value;
}

void wacom_input_sync(void *wcombo)
{
	input_sync(get_input_dev((struct wacom_combo *)wcombo));
}

static int wacom_open(struct input_dev *dev)
{
	struct wacom *wacom = input_get_drvdata(dev);

	mutex_lock(&wacom->lock);

	wacom->irq->dev = wacom->usbdev;

	if (usb_autopm_get_interface(wacom->intf) < 0) {
		mutex_unlock(&wacom->lock);
		return -EIO;
	}

	if (usb_submit_urb(wacom->irq, GFP_KERNEL)) {
		usb_autopm_put_interface(wacom->intf);
		mutex_unlock(&wacom->lock);
		return -EIO;
	}

	wacom->open = 1;
	wacom->intf->needs_remote_wakeup = 1;

	mutex_unlock(&wacom->lock);
	return 0;
}

static void wacom_close(struct input_dev *dev)
{
	struct wacom *wacom = input_get_drvdata(dev);

	mutex_lock(&wacom->lock);
	usb_kill_urb(wacom->irq);
	wacom->open = 0;
	wacom->intf->needs_remote_wakeup = 0;
	mutex_unlock(&wacom->lock);
}

void input_dev_mo(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->keybit[BIT_WORD(BTN_MISC)] |= BIT_MASK(BTN_1) |
		BIT_MASK(BTN_5);
	input_set_abs_params(input_dev, ABS_WHEEL, 0, 71, 0, 0);
}

void input_dev_g4(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->evbit[0] |= BIT_MASK(EV_MSC);
	input_dev->mscbit[0] |= BIT_MASK(MSC_SERIAL);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_FINGER);
	input_dev->keybit[BIT_WORD(BTN_MISC)] |= BIT_MASK(BTN_0) |
		BIT_MASK(BTN_4);
}

void input_dev_g(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->evbit[0] |= BIT_MASK(EV_REL);
	input_dev->relbit[0] |= BIT_MASK(REL_WHEEL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_RUBBER) |
		BIT_MASK(BTN_TOOL_PEN) | BIT_MASK(BTN_STYLUS) |
		BIT_MASK(BTN_TOOL_MOUSE) | BIT_MASK(BTN_STYLUS2);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, wacom_wac->features->distance_max, 0, 0);
}

void input_dev_i3s(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_FINGER);
	input_dev->keybit[BIT_WORD(BTN_MISC)] |= BIT_MASK(BTN_0) |
		BIT_MASK(BTN_1) | BIT_MASK(BTN_2) | BIT_MASK(BTN_3);
	input_set_abs_params(input_dev, ABS_RX, 0, 4096, 0, 0);
	input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);
}

void input_dev_i3(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->keybit[BIT_WORD(BTN_MISC)] |= BIT_MASK(BTN_4) |
		BIT_MASK(BTN_5) | BIT_MASK(BTN_6) | BIT_MASK(BTN_7);
	input_set_abs_params(input_dev, ABS_RY, 0, 4096, 0, 0);
}

void input_dev_i4s(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_FINGER);
	input_dev->keybit[BIT_WORD(BTN_MISC)] |= BIT_MASK(BTN_0) | BIT_MASK(BTN_1) | BIT_MASK(BTN_2) | BIT_MASK(BTN_3);
	input_dev->keybit[BIT_WORD(BTN_MISC)] |= BIT_MASK(BTN_4) | BIT_MASK(BTN_5) | BIT_MASK(BTN_6);
	input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);
}

void input_dev_i4(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->keybit[BIT_WORD(BTN_MISC)] |= BIT_MASK(BTN_7) | BIT_MASK(BTN_8);
}

void input_dev_bee(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->keybit[BIT_WORD(BTN_MISC)] |= BIT_MASK(BTN_8) | BIT_MASK(BTN_9);
}

void input_dev_i(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->evbit[0] |= BIT_MASK(EV_MSC) | BIT_MASK(EV_REL);
	input_dev->mscbit[0] |= BIT_MASK(MSC_SERIAL);
	input_dev->relbit[0] |= BIT_MASK(REL_WHEEL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE) |
		BIT_MASK(BTN_SIDE) | BIT_MASK(BTN_EXTRA);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_RUBBER) |
		BIT_MASK(BTN_TOOL_PEN) | BIT_MASK(BTN_STYLUS) |
		BIT_MASK(BTN_TOOL_MOUSE) | BIT_MASK(BTN_TOOL_BRUSH) |
		BIT_MASK(BTN_TOOL_PENCIL) | BIT_MASK(BTN_TOOL_AIRBRUSH) |
		BIT_MASK(BTN_TOOL_LENS) | BIT_MASK(BTN_STYLUS2);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, wacom_wac->features->distance_max, 0, 0);
	input_set_abs_params(input_dev, ABS_WHEEL, 0, 1023, 0, 0);
	input_set_abs_params(input_dev, ABS_TILT_X, 0, 127, 0, 0);
	input_set_abs_params(input_dev, ABS_TILT_Y, 0, 127, 0, 0);
	input_set_abs_params(input_dev, ABS_RZ, -900, 899, 0, 0);
	input_set_abs_params(input_dev, ABS_THROTTLE, -1023, 1023, 0, 0);
}

void input_dev_pl(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_PEN) |
		BIT_MASK(BTN_STYLUS) | BIT_MASK(BTN_STYLUS2);
}

void input_dev_pt(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_RUBBER);
}

void input_dev_tpc(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	if (wacom_wac->features->device_type == BTN_TOOL_DOUBLETAP ||
	    wacom_wac->features->device_type == BTN_TOOL_TRIPLETAP) {
		input_set_abs_params(input_dev, ABS_RX, 0, wacom_wac->features->x_phy, 0, 0);
		input_set_abs_params(input_dev, ABS_RY, 0, wacom_wac->features->y_phy, 0, 0);
		input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_DOUBLETAP);
	}
}

void input_dev_tpc2fg(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	if (wacom_wac->features->device_type == BTN_TOOL_TRIPLETAP) {
		input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_TRIPLETAP);
		input_dev->evbit[0] |= BIT_MASK(EV_MSC);
		input_dev->mscbit[0] |= BIT_MASK(MSC_SERIAL);
	}
}

static int wacom_parse_hid(struct usb_interface *intf, struct hid_descriptor *hid_desc,
			   struct wacom_features *features)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	char limit = 0;
	/* result has to be defined as int for some devices */
	int result = 0;
	int i = 0, usage = WCM_UNDEFINED, finger = 0, pen = 0;
	unsigned char *report;

	report = kzalloc(hid_desc->wDescriptorLength, GFP_KERNEL);
	if (!report)
		return -ENOMEM;

	/* retrive report descriptors */
	do {
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			USB_REQ_GET_DESCRIPTOR,
			USB_RECIP_INTERFACE | USB_DIR_IN,
			HID_DEVICET_REPORT << 8,
			intf->altsetting[0].desc.bInterfaceNumber, /* interface */
			report,
			hid_desc->wDescriptorLength,
			5000); /* 5 secs */
	} while (result < 0 && limit++ < 5);

	/* No need to parse the Descriptor. It isn't an error though */
	if (result < 0)
		goto out;

	for (i = 0; i < hid_desc->wDescriptorLength; i++) {

		switch (report[i]) {
		case HID_USAGE_PAGE:
			switch (report[i + 1]) {
			case HID_USAGE_PAGE_DIGITIZER:
				usage = WCM_DIGITIZER;
				i++;
				break;

			case HID_USAGE_PAGE_DESKTOP:
				usage = WCM_DESKTOP;
				i++;
				break;
			}
			break;

		case HID_USAGE:
			switch (report[i + 1]) {
			case HID_USAGE_X:
				if (usage == WCM_DESKTOP) {
					if (finger) {
						features->device_type = BTN_TOOL_DOUBLETAP;
						if (features->type == TABLETPC2FG) {
							/* need to reset back */
							features->pktlen = WACOM_PKGLEN_TPC2FG;
							features->device_type = BTN_TOOL_TRIPLETAP;
						}
						features->x_max =
							wacom_le16_to_cpu(&report[i + 3]);
						features->x_phy =
							wacom_le16_to_cpu(&report[i + 6]);
						features->unit = report[i + 9];
						features->unitExpo = report[i + 11];
						i += 12;
					} else if (pen) {
						/* penabled only accepts exact bytes of data */
						if (features->type == TABLETPC2FG)
							features->pktlen = WACOM_PKGLEN_PENABLED;
						features->device_type = BTN_TOOL_PEN;
						features->x_max =
							wacom_le16_to_cpu(&report[i + 3]);
						i += 4;
					}
				} else if (usage == WCM_DIGITIZER) {
					/* max pressure isn't reported
					features->pressure_max = (unsigned short)
							(report[i+4] << 8  | report[i + 3]);
					*/
					features->pressure_max = 255;
					i += 4;
				}
				break;

			case HID_USAGE_Y:
				if (usage == WCM_DESKTOP) {
					if (finger) {
						features->device_type = BTN_TOOL_DOUBLETAP;
						if (features->type == TABLETPC2FG) {
							/* need to reset back */
							features->pktlen = WACOM_PKGLEN_TPC2FG;
							features->device_type = BTN_TOOL_TRIPLETAP;
							features->y_max =
								wacom_le16_to_cpu(&report[i + 3]);
							features->y_phy =
								wacom_le16_to_cpu(&report[i + 6]);
							i += 7;
						} else {
							features->y_max =
								features->x_max;
							features->y_phy =
								wacom_le16_to_cpu(&report[i + 3]);
							i += 4;
						}
					} else if (pen) {
						/* penabled only accepts exact bytes of data */
						if (features->type == TABLETPC2FG)
							features->pktlen = WACOM_PKGLEN_PENABLED;
						features->device_type = BTN_TOOL_PEN;
						features->y_max =
							wacom_le16_to_cpu(&report[i + 3]);
						i += 4;
					}
				}
				break;

			case HID_USAGE_FINGER:
				finger = 1;
				i++;
				break;

			case HID_USAGE_STYLUS:
				pen = 1;
				i++;
				break;

			case HID_USAGE_UNDEFINED:
				if (usage == WCM_DESKTOP && finger) /* capacity */
					features->pressure_max =
						wacom_le16_to_cpu(&report[i + 3]);
				i += 4;
				break;
			}
			break;

		case HID_COLLECTION:
			/* reset UsagePage and Finger */
			finger = usage = 0;
			break;
		}
	}

 out:
	result = 0;
	kfree(report);
	return result;
}

static int wacom_query_tablet_data(struct usb_interface *intf, struct wacom_features *features)
{
	unsigned char *rep_data;
	int limit = 0, report_id = 2;
	int error = -ENOMEM;

	rep_data = kmalloc(2, GFP_KERNEL);
	if (!rep_data)
		return error;

	/* ask to report tablet data if it is 2FGT or not a Tablet PC */
	if (features->device_type == BTN_TOOL_TRIPLETAP) {
		do {
			rep_data[0] = 3;
			rep_data[1] = 4;
			report_id = 3;
			error = usb_set_report(intf, WAC_HID_FEATURE_REPORT,
				report_id, rep_data, 2);
			if (error >= 0)
				error = usb_get_report(intf,
					WAC_HID_FEATURE_REPORT, report_id,
					rep_data, 3);
		} while ((error < 0 || rep_data[1] != 4) && limit++ < 5);
	} else if (features->type != TABLETPC && features->type != TABLETPC2FG) {
		do {
			rep_data[0] = 2;
			rep_data[1] = 2;
			error = usb_set_report(intf, WAC_HID_FEATURE_REPORT,
				report_id, rep_data, 2);
			if (error >= 0)
				error = usb_get_report(intf,
					WAC_HID_FEATURE_REPORT, report_id,
					rep_data, 2);
		} while ((error < 0 || rep_data[1] != 2) && limit++ < 5);
	}

	kfree(rep_data);

	return error < 0 ? error : 0;
}

static int wacom_retrieve_hid_descriptor(struct usb_interface *intf,
		struct wacom_features *features)
{
	int error = 0;
	struct usb_host_interface *interface = intf->cur_altsetting;
	struct hid_descriptor *hid_desc;

	/* default device to penabled */
	features->device_type = BTN_TOOL_PEN;

	/* only Tablet PCs need to retrieve the info */
	if ((features->type != TABLETPC) && (features->type != TABLETPC2FG))
		goto out;

	if (usb_get_extra_descriptor(interface, HID_DEVICET_HID, &hid_desc)) {
		if (usb_get_extra_descriptor(&interface->endpoint[0],
				HID_DEVICET_REPORT, &hid_desc)) {
			printk("wacom: can not retrieve extra class descriptor\n");
			error = 1;
			goto out;
		}
	}
	error = wacom_parse_hid(intf, hid_desc, features);
	if (error)
		goto out;

	/* touch device found but size is not defined. use default */
	if (features->device_type == BTN_TOOL_DOUBLETAP && !features->x_max) {
		features->x_max = 1023;
		features->y_max = 1023;
	}

 out:
	return error;
}

static int wacom_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct wacom *wacom;
	struct wacom_wac *wacom_wac;
	struct wacom_features *features;
	struct input_dev *input_dev;
	int error = -ENOMEM;

	wacom = kzalloc(sizeof(struct wacom), GFP_KERNEL);
	wacom_wac = kzalloc(sizeof(struct wacom_wac), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!wacom || !input_dev || !wacom_wac)
		goto fail1;

	wacom_wac->data = usb_buffer_alloc(dev, WACOM_PKGLEN_MAX, GFP_KERNEL, &wacom->data_dma);
	if (!wacom_wac->data)
		goto fail1;

	wacom->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!wacom->irq)
		goto fail2;

	wacom->usbdev = dev;
	wacom->dev = input_dev;
	wacom->intf = intf;
	mutex_init(&wacom->lock);
	usb_make_path(dev, wacom->phys, sizeof(wacom->phys));
	strlcat(wacom->phys, "/input0", sizeof(wacom->phys));

	wacom_wac->features = features = get_wacom_feature(id);
	BUG_ON(features->pktlen > WACOM_PKGLEN_MAX);

	input_dev->name = wacom_wac->features->name;
	wacom->wacom_wac = wacom_wac;
	usb_to_input_id(dev, &input_dev->id);

	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, wacom);

	input_dev->open = wacom_open;
	input_dev->close = wacom_close;

	endpoint = &intf->cur_altsetting->endpoint[0].desc;

	/* Retrieve the physical and logical size for OEM devices */
	error = wacom_retrieve_hid_descriptor(intf, features);
	if (error)
		goto fail2;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, features->x_max, 4, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, features->y_max, 4, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, features->pressure_max, 0, 0);
	input_dev->absbit[BIT_WORD(ABS_MISC)] |= BIT_MASK(ABS_MISC);

	wacom_init_input_dev(input_dev, wacom_wac);

	usb_fill_int_urb(wacom->irq, dev,
			 usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			 wacom_wac->data, features->pktlen,
			 wacom_sys_irq, wacom, endpoint->bInterval);
	wacom->irq->transfer_dma = wacom->data_dma;
	wacom->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(wacom->dev);
	if (error)
		goto fail3;

	/* Note that if query fails it is not a hard failure */
	wacom_query_tablet_data(intf, features);

	usb_set_intfdata(intf, wacom);
	return 0;

 fail3:	usb_free_urb(wacom->irq);
 fail2:	usb_buffer_free(dev, WACOM_PKGLEN_MAX, wacom_wac->data, wacom->data_dma);
 fail1:	input_free_device(input_dev);
	kfree(wacom);
	kfree(wacom_wac);
	return error;
}

static void wacom_disconnect(struct usb_interface *intf)
{
	struct wacom *wacom = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	usb_kill_urb(wacom->irq);
	input_unregister_device(wacom->dev);
	usb_free_urb(wacom->irq);
	usb_buffer_free(interface_to_usbdev(intf), WACOM_PKGLEN_MAX,
			wacom->wacom_wac->data, wacom->data_dma);
	kfree(wacom->wacom_wac);
	kfree(wacom);
}

static int wacom_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct wacom *wacom = usb_get_intfdata(intf);

	mutex_lock(&wacom->lock);
	usb_kill_urb(wacom->irq);
	mutex_unlock(&wacom->lock);

	return 0;
}

static int wacom_resume(struct usb_interface *intf)
{
	struct wacom *wacom = usb_get_intfdata(intf);
	struct wacom_features *features = wacom->wacom_wac->features;
	int rv;

	mutex_lock(&wacom->lock);
	if (wacom->open) {
		rv = usb_submit_urb(wacom->irq, GFP_NOIO);
		/* switch to wacom mode if needed */
		if (!wacom_retrieve_hid_descriptor(intf, features))
			wacom_query_tablet_data(intf, features);
	} else
		rv = 0;
	mutex_unlock(&wacom->lock);

	return rv;
}

static int wacom_reset_resume(struct usb_interface *intf)
{
	return wacom_resume(intf);
}

static struct usb_driver wacom_driver = {
	.name =		"wacom",
	.probe =	wacom_probe,
	.disconnect =	wacom_disconnect,
	.suspend =	wacom_suspend,
	.resume =	wacom_resume,
	.reset_resume =	wacom_reset_resume,
	.supports_autosuspend = 1,
};

static int __init wacom_init(void)
{
	int result;
	wacom_driver.id_table = get_device_table();
	result = usb_register(&wacom_driver);
	if (result == 0)
		printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
		       DRIVER_DESC "\n");
	return result;
}

static void __exit wacom_exit(void)
{
	usb_deregister(&wacom_driver);
}

module_init(wacom_init);
module_exit(wacom_exit);
