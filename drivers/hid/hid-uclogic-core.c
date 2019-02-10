// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID driver for UC-Logic devices not fully compliant with HID standard
 *
 *  Copyright (c) 2010-2014 Nikolai Kondrashov
 *  Copyright (c) 2013 Martin Rusko
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>
#include "usbhid/usbhid.h"
#include "hid-uclogic-rdesc.h"

#include "hid-ids.h"

/* Parameter indices */
enum uclogic_prm {
	UCLOGIC_PRM_X_LM	= 1,
	UCLOGIC_PRM_Y_LM	= 2,
	UCLOGIC_PRM_PRESSURE_LM	= 4,
	UCLOGIC_PRM_RESOLUTION	= 5,
	UCLOGIC_PRM_NUM
};

/* Driver data */
struct uclogic_drvdata {
	__u8 *rdesc;
	unsigned int rsize;
	bool invert_pen_inrange;
	bool ignore_pen_usage;
	bool has_virtual_pad_interface;
};

static __u8 *uclogic_report_fixup(struct hid_device *hdev, __u8 *rdesc,
					unsigned int *rsize)
{
	struct usb_interface *iface = to_usb_interface(hdev->dev.parent);
	__u8 iface_num = iface->cur_altsetting->desc.bInterfaceNumber;
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->rdesc != NULL) {
		rdesc = drvdata->rdesc;
		*rsize = drvdata->rsize;
		return rdesc;
	}

	switch (hdev->product) {
	case USB_DEVICE_ID_UCLOGIC_TABLET_PF1209:
		if (*rsize == UCLOGIC_RDESC_PF1209_ORIG_SIZE) {
			rdesc = uclogic_rdesc_pf1209_fixed_arr;
			*rsize = uclogic_rdesc_pf1209_fixed_size;
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_WP4030U:
		if (*rsize == UCLOGIC_RDESC_WPXXXXU_ORIG_SIZE) {
			rdesc = uclogic_rdesc_wp4030u_fixed_arr;
			*rsize = uclogic_rdesc_wp4030u_fixed_size;
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_WP5540U:
		if (*rsize == UCLOGIC_RDESC_WPXXXXU_ORIG_SIZE) {
			rdesc = uclogic_rdesc_wp5540u_fixed_arr;
			*rsize = uclogic_rdesc_wp5540u_fixed_size;
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_WP8060U:
		if (*rsize == UCLOGIC_RDESC_WPXXXXU_ORIG_SIZE) {
			rdesc = uclogic_rdesc_wp8060u_fixed_arr;
			*rsize = uclogic_rdesc_wp8060u_fixed_size;
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_WP1062:
		if (*rsize == UCLOGIC_RDESC_WP1062_ORIG_SIZE) {
			rdesc = uclogic_rdesc_wp1062_fixed_arr;
			*rsize = uclogic_rdesc_wp1062_fixed_size;
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_WIRELESS_TABLET_TWHL850:
		switch (iface_num) {
		case 0:
			if (*rsize == UCLOGIC_RDESC_TWHL850_ORIG0_SIZE) {
				rdesc = uclogic_rdesc_twhl850_fixed0_arr;
				*rsize = uclogic_rdesc_twhl850_fixed0_size;
			}
			break;
		case 1:
			if (*rsize == UCLOGIC_RDESC_TWHL850_ORIG1_SIZE) {
				rdesc = uclogic_rdesc_twhl850_fixed1_arr;
				*rsize = uclogic_rdesc_twhl850_fixed1_size;
			}
			break;
		case 2:
			if (*rsize == UCLOGIC_RDESC_TWHL850_ORIG2_SIZE) {
				rdesc = uclogic_rdesc_twhl850_fixed2_arr;
				*rsize = uclogic_rdesc_twhl850_fixed2_size;
			}
			break;
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_TWHA60:
		switch (iface_num) {
		case 0:
			if (*rsize == UCLOGIC_RDESC_TWHA60_ORIG0_SIZE) {
				rdesc = uclogic_rdesc_twha60_fixed0_arr;
				*rsize = uclogic_rdesc_twha60_fixed0_size;
			}
			break;
		case 1:
			if (*rsize == UCLOGIC_RDESC_TWHA60_ORIG1_SIZE) {
				rdesc = uclogic_rdesc_twha60_fixed1_arr;
				*rsize = uclogic_rdesc_twha60_fixed1_size;
			}
			break;
		}
		break;
	}

	return rdesc;
}

static int uclogic_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);

	/* discard the unused pen interface */
	if ((drvdata->ignore_pen_usage) &&
	    (field->application == HID_DG_PEN))
		return -1;

	/* let hid-core decide what to do */
	return 0;
}

static int uclogic_input_configured(struct hid_device *hdev,
		struct hid_input *hi)
{
	char *name;
	const char *suffix = NULL;
	struct hid_field *field;
	size_t len;

	/* no report associated (HID_QUIRK_MULTI_INPUT not set) */
	if (!hi->report)
		return 0;

	field = hi->report->field[0];

	switch (field->application) {
	case HID_GD_KEYBOARD:
		suffix = "Keyboard";
		break;
	case HID_GD_MOUSE:
		suffix = "Mouse";
		break;
	case HID_GD_KEYPAD:
		suffix = "Pad";
		break;
	case HID_DG_PEN:
		suffix = "Pen";
		break;
	case HID_CP_CONSUMER_CONTROL:
		suffix = "Consumer Control";
		break;
	case HID_GD_SYSTEM_CONTROL:
		suffix = "System Control";
		break;
	}

	if (suffix) {
		len = strlen(hdev->name) + 2 + strlen(suffix);
		name = devm_kzalloc(&hi->input->dev, len, GFP_KERNEL);
		if (name) {
			snprintf(name, len, "%s %s", hdev->name, suffix);
			hi->input->name = name;
		}
	}

	return 0;
}

/**
 * Enable fully-functional tablet mode and determine device parameters.
 *
 * @hdev:	HID device
 */
static int uclogic_tablet_enable(struct hid_device *hdev)
{
	int rc;
	struct usb_device *usb_dev = hid_to_usb_dev(hdev);
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);
	__le16 *buf = NULL;
	size_t len;
	s32 params[UCLOGIC_RDESC_PEN_PH_ID_NUM];
	s32 resolution;

	/*
	 * Read string descriptor containing tablet parameters. The specific
	 * string descriptor and data were discovered by sniffing the Windows
	 * driver traffic.
	 * NOTE: This enables fully-functional tablet mode.
	 */
	len = UCLOGIC_PRM_NUM * sizeof(*buf);
	buf = kmalloc(len, GFP_KERNEL);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto cleanup;
	}
	rc = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
				USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
				(USB_DT_STRING << 8) + 0x64,
				0x0409, buf, len,
				USB_CTRL_GET_TIMEOUT);
	if (rc == -EPIPE) {
		hid_err(hdev, "device parameters not found\n");
		rc = -ENODEV;
		goto cleanup;
	} else if (rc < 0) {
		hid_err(hdev, "failed to get device parameters: %d\n", rc);
		rc = -ENODEV;
		goto cleanup;
	} else if (rc != len) {
		hid_err(hdev, "invalid device parameters\n");
		rc = -ENODEV;
		goto cleanup;
	}

	/* Extract device parameters */
	params[UCLOGIC_RDESC_PEN_PH_ID_X_LM] =
		le16_to_cpu(buf[UCLOGIC_PRM_X_LM]);
	params[UCLOGIC_RDESC_PEN_PH_ID_Y_LM] =
		le16_to_cpu(buf[UCLOGIC_PRM_Y_LM]);
	params[UCLOGIC_RDESC_PEN_PH_ID_PRESSURE_LM] =
		le16_to_cpu(buf[UCLOGIC_PRM_PRESSURE_LM]);
	resolution = le16_to_cpu(buf[UCLOGIC_PRM_RESOLUTION]);
	if (resolution == 0) {
		params[UCLOGIC_RDESC_PEN_PH_ID_X_PM] = 0;
		params[UCLOGIC_RDESC_PEN_PH_ID_Y_PM] = 0;
	} else {
		params[UCLOGIC_RDESC_PEN_PH_ID_X_PM] =
			params[UCLOGIC_RDESC_PEN_PH_ID_X_LM] *
			1000 / resolution;
		params[UCLOGIC_RDESC_PEN_PH_ID_Y_PM] =
			params[UCLOGIC_RDESC_PEN_PH_ID_Y_LM] *
			1000 / resolution;
	}

	/* Format fixed report descriptor */
	drvdata->rdesc = uclogic_rdesc_template_apply(
				uclogic_rdesc_pen_template_arr,
				uclogic_rdesc_pen_template_size,
				params, ARRAY_SIZE(params));
	if (drvdata->rdesc == NULL) {
		rc = -ENOMEM;
		goto cleanup;
	}
	drvdata->rsize = uclogic_rdesc_pen_template_size;

	rc = 0;

cleanup:
	kfree(buf);
	return rc;
}

/**
 * Enable actual button mode.
 *
 * @hdev:	HID device
 */
static int uclogic_button_enable(struct hid_device *hdev)
{
	int rc;
	struct usb_device *usb_dev = hid_to_usb_dev(hdev);
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);
	char *str_buf;
	size_t str_len = 16;
	unsigned char *rdesc;
	size_t rdesc_len;

	str_buf = kzalloc(str_len, GFP_KERNEL);
	if (str_buf == NULL) {
		rc = -ENOMEM;
		goto cleanup;
	}

	/* Enable abstract keyboard mode */
	rc = usb_string(usb_dev, 0x7b, str_buf, str_len);
	if (rc == -EPIPE) {
		hid_info(hdev, "button mode setting not found\n");
		rc = 0;
		goto cleanup;
	} else if (rc < 0) {
		hid_err(hdev, "failed to enable abstract keyboard\n");
		goto cleanup;
	} else if (strncmp(str_buf, "HK On", rc)) {
		hid_info(hdev, "invalid answer when requesting buttons: '%s'\n",
			str_buf);
		rc = -EINVAL;
		goto cleanup;
	}

	/* Re-allocate fixed report descriptor */
	rdesc_len = drvdata->rsize + uclogic_rdesc_buttonpad_size;
	rdesc = devm_kzalloc(&hdev->dev, rdesc_len, GFP_KERNEL);
	if (!rdesc) {
		rc = -ENOMEM;
		goto cleanup;
	}

	memcpy(rdesc, drvdata->rdesc, drvdata->rsize);

	/* Append the buttonpad descriptor */
	memcpy(rdesc + drvdata->rsize, uclogic_rdesc_buttonpad_arr,
	       uclogic_rdesc_buttonpad_size);

	/* clean up old rdesc and use the new one */
	drvdata->rsize = rdesc_len;
	devm_kfree(&hdev->dev, drvdata->rdesc);
	drvdata->rdesc = rdesc;

	rc = 0;

cleanup:
	kfree(str_buf);
	return rc;
}

static int uclogic_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int rc;
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *udev = hid_to_usb_dev(hdev);
	struct uclogic_drvdata *drvdata;

	/*
	 * libinput requires the pad interface to be on a different node
	 * than the pen, so use QUIRK_MULTI_INPUT for all tablets.
	 */
	hdev->quirks |= HID_QUIRK_MULTI_INPUT;

	/* Allocate and assign driver data */
	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL)
		return -ENOMEM;

	hid_set_drvdata(hdev, drvdata);

	switch (id->product) {
	case USB_DEVICE_ID_HUION_TABLET:
	case USB_DEVICE_ID_YIYNOVA_TABLET:
	case USB_DEVICE_ID_UCLOGIC_UGEE_TABLET_81:
	case USB_DEVICE_ID_UCLOGIC_DRAWIMAGE_G3:
	case USB_DEVICE_ID_UCLOGIC_UGEE_TABLET_45:
		/* If this is the pen interface */
		if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
			rc = uclogic_tablet_enable(hdev);
			if (rc) {
				hid_err(hdev, "tablet enabling failed\n");
				return rc;
			}
			drvdata->invert_pen_inrange = true;

			rc = uclogic_button_enable(hdev);
			drvdata->has_virtual_pad_interface = !rc;
		} else {
			drvdata->ignore_pen_usage = true;
		}
		break;
	case USB_DEVICE_ID_UGTIZER_TABLET_GP0610:
	case USB_DEVICE_ID_UGEE_TABLET_EX07S:
		/* If this is the pen interface */
		if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
			rc = uclogic_tablet_enable(hdev);
			if (rc) {
				hid_err(hdev, "tablet enabling failed\n");
				return rc;
			}
			drvdata->invert_pen_inrange = true;
		} else {
			drvdata->ignore_pen_usage = true;
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_TWHA60:
		/*
		 * If it is the three-interface version, which is known to
		 * respond to initialization.
		 */
		if (udev->config->desc.bNumInterfaces == 3) {
			/* If it is the pen interface */
			if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
				rc = uclogic_tablet_enable(hdev);
				if (rc) {
					hid_err(hdev, "tablet enabling failed\n");
					return rc;
				}
				drvdata->invert_pen_inrange = true;

				rc = uclogic_button_enable(hdev);
				drvdata->has_virtual_pad_interface = !rc;
			} else {
				drvdata->ignore_pen_usage = true;
			}
		}
		break;
	}

	rc = hid_parse(hdev);
	if (rc) {
		hid_err(hdev, "parse failed\n");
		return rc;
	}

	rc = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (rc) {
		hid_err(hdev, "hw start failed\n");
		return rc;
	}

	return 0;
}

static int uclogic_raw_event(struct hid_device *hdev, struct hid_report *report,
			u8 *data, int size)
{
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);

	if ((report->type == HID_INPUT_REPORT) &&
	    (report->id == UCLOGIC_RDESC_PEN_ID) &&
	    (size >= 2)) {
		if (drvdata->has_virtual_pad_interface && (data[1] & 0x20))
			/* Change to virtual frame button report ID */
			data[0] = 0xf7;
		else if (drvdata->invert_pen_inrange)
			/* Invert the in-range bit */
			data[1] ^= 0x40;
	}

	return 0;
}

static const struct hid_device_id uclogic_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_PF1209) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP4030U) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP5540U) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP8060U) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP1062) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_WIRELESS_TABLET_TWHL850) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_TWHA60) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_HUION, USB_DEVICE_ID_HUION_TABLET) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC, USB_DEVICE_ID_HUION_TABLET) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC, USB_DEVICE_ID_YIYNOVA_TABLET) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC, USB_DEVICE_ID_UCLOGIC_UGEE_TABLET_81) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC, USB_DEVICE_ID_UCLOGIC_UGEE_TABLET_45) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC, USB_DEVICE_ID_UCLOGIC_DRAWIMAGE_G3) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGTIZER, USB_DEVICE_ID_UGTIZER_TABLET_GP0610) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE, USB_DEVICE_ID_UGEE_TABLET_EX07S) },
	{ }
};
MODULE_DEVICE_TABLE(hid, uclogic_devices);

static struct hid_driver uclogic_driver = {
	.name = "uclogic",
	.id_table = uclogic_devices,
	.probe = uclogic_probe,
	.report_fixup = uclogic_report_fixup,
	.raw_event = uclogic_raw_event,
	.input_mapping = uclogic_input_mapping,
	.input_configured = uclogic_input_configured,
};
module_hid_driver(uclogic_driver);

MODULE_AUTHOR("Martin Rusko");
MODULE_AUTHOR("Nikolai Kondrashov");
MODULE_LICENSE("GPL");
