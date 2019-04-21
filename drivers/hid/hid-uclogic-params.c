// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID driver for UC-Logic devices not fully compliant with HID standard
 *  - tablet initialization and parameter retrieval
 *
 *  Copyright (c) 2018 Nikolai Kondrashov
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "hid-uclogic-params.h"
#include "hid-uclogic-rdesc.h"
#include "usbhid/usbhid.h"
#include "hid-ids.h"
#include <linux/ctype.h>
#include <asm/unaligned.h>

/**
 * Convert a pen in-range reporting type to a string.
 *
 * @inrange:	The in-range reporting type to convert.
 *
 * Returns:
 *	The string representing the type, or NULL if the type is unknown.
 */
const char *uclogic_params_pen_inrange_to_str(
			enum uclogic_params_pen_inrange inrange)
{
	switch (inrange) {
	case UCLOGIC_PARAMS_PEN_INRANGE_NORMAL:
		return "normal";
	case UCLOGIC_PARAMS_PEN_INRANGE_INVERTED:
		return "inverted";
	case UCLOGIC_PARAMS_PEN_INRANGE_NONE:
		return "none";
	default:
		return NULL;
	}
}

/**
 * uclogic_params_get_str_desc - retrieve a string descriptor from a HID
 * device interface, putting it into a kmalloc-allocated buffer as is, without
 * character encoding conversion.
 *
 * @pbuf:	Location for the kmalloc-allocated buffer pointer containing
 *		the retrieved descriptor. Not modified in case of error.
 *		Can be NULL to have retrieved descriptor discarded.
 * @hdev:	The HID device of the tablet interface to retrieve the string
 *		descriptor from. Cannot be NULL.
 * @idx:	Index of the string descriptor to request from the device.
 * @len:	Length of the buffer to allocate and the data to retrieve.
 *
 * Returns:
 *	number of bytes retrieved (<= len),
 *	-EPIPE, if the descriptor was not found, or
 *	another negative errno code in case of other error.
 */
static int uclogic_params_get_str_desc(__u8 **pbuf, struct hid_device *hdev,
					__u8 idx, size_t len)
{
	int rc;
	struct usb_device *udev = hid_to_usb_dev(hdev);
	__u8 *buf = NULL;

	/* Check arguments */
	if (hdev == NULL) {
		rc = -EINVAL;
		goto cleanup;
	}

	buf = kmalloc(len, GFP_KERNEL);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto cleanup;
	}

	rc = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
				(USB_DT_STRING << 8) + idx,
				0x0409, buf, len,
				USB_CTRL_GET_TIMEOUT);
	if (rc == -EPIPE) {
		hid_dbg(hdev, "string descriptor #%hhu not found\n", idx);
		goto cleanup;
	} else if (rc < 0) {
		hid_err(hdev,
			"failed retrieving string descriptor #%hhu: %d\n",
			idx, rc);
		goto cleanup;
	}

	if (pbuf != NULL) {
		*pbuf = buf;
		buf = NULL;
	}

cleanup:
	kfree(buf);
	return rc;
}

/**
 * uclogic_params_pen_cleanup - free resources used by struct
 * uclogic_params_pen (tablet interface's pen input parameters).
 * Can be called repeatedly.
 *
 * @pen:	Pen input parameters to cleanup. Cannot be NULL.
 */
static void uclogic_params_pen_cleanup(struct uclogic_params_pen *pen)
{
	kfree(pen->desc_ptr);
	memset(pen, 0, sizeof(*pen));
}

/**
 * uclogic_params_pen_init_v1() - initialize tablet interface pen
 * input and retrieve its parameters from the device, using v1 protocol.
 *
 * @pen:	Pointer to the pen parameters to initialize (to be
 *		cleaned up with uclogic_params_pen_cleanup()). Not modified in
 *		case of error, or if parameters are not found. Cannot be NULL.
 * @pfound:	Location for a flag which is set to true if the parameters
 *		were found, and to false if not (e.g. device was
 *		incompatible). Not modified in case of error. Cannot be NULL.
 * @hdev:	The HID device of the tablet interface to initialize and get
 *		parameters from. Cannot be NULL.
 *
 * Returns:
 *	Zero, if successful. A negative errno code on error.
 */
static int uclogic_params_pen_init_v1(struct uclogic_params_pen *pen,
				      bool *pfound,
				      struct hid_device *hdev)
{
	int rc;
	bool found = false;
	/* Buffer for (part of) the string descriptor */
	__u8 *buf = NULL;
	/* Minimum descriptor length required, maximum seen so far is 18 */
	const int len = 12;
	s32 resolution;
	/* Pen report descriptor template parameters */
	s32 desc_params[UCLOGIC_RDESC_PEN_PH_ID_NUM];
	__u8 *desc_ptr = NULL;

	/* Check arguments */
	if (pen == NULL || pfound == NULL || hdev == NULL) {
		rc = -EINVAL;
		goto cleanup;
	}

	/*
	 * Read string descriptor containing pen input parameters.
	 * The specific string descriptor and data were discovered by sniffing
	 * the Windows driver traffic.
	 * NOTE: This enables fully-functional tablet mode.
	 */
	rc = uclogic_params_get_str_desc(&buf, hdev, 100, len);
	if (rc == -EPIPE) {
		hid_dbg(hdev,
			"string descriptor with pen parameters not found, assuming not compatible\n");
		goto finish;
	} else if (rc < 0) {
		hid_err(hdev, "failed retrieving pen parameters: %d\n", rc);
		goto cleanup;
	} else if (rc != len) {
		hid_dbg(hdev,
			"string descriptor with pen parameters has invalid length (got %d, expected %d), assuming not compatible\n",
			rc, len);
		goto finish;
	}

	/*
	 * Fill report descriptor parameters from the string descriptor
	 */
	desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_LM] =
		get_unaligned_le16(buf + 2);
	desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_LM] =
		get_unaligned_le16(buf + 4);
	desc_params[UCLOGIC_RDESC_PEN_PH_ID_PRESSURE_LM] =
		get_unaligned_le16(buf + 8);
	resolution = get_unaligned_le16(buf + 10);
	if (resolution == 0) {
		desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_PM] = 0;
		desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_PM] = 0;
	} else {
		desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_PM] =
			desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_LM] * 1000 /
			resolution;
		desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_PM] =
			desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_LM] * 1000 /
			resolution;
	}
	kfree(buf);
	buf = NULL;

	/*
	 * Generate pen report descriptor
	 */
	desc_ptr = uclogic_rdesc_template_apply(
				uclogic_rdesc_pen_v1_template_arr,
				uclogic_rdesc_pen_v1_template_size,
				desc_params, ARRAY_SIZE(desc_params));
	if (desc_ptr == NULL) {
		rc = -ENOMEM;
		goto cleanup;
	}

	/*
	 * Fill-in the parameters
	 */
	memset(pen, 0, sizeof(*pen));
	pen->desc_ptr = desc_ptr;
	desc_ptr = NULL;
	pen->desc_size = uclogic_rdesc_pen_v1_template_size;
	pen->id = UCLOGIC_RDESC_PEN_V1_ID;
	pen->inrange = UCLOGIC_PARAMS_PEN_INRANGE_INVERTED;
	found = true;
finish:
	*pfound = found;
	rc = 0;
cleanup:
	kfree(desc_ptr);
	kfree(buf);
	return rc;
}

/**
 * uclogic_params_get_le24() - get a 24-bit little-endian number from a
 * buffer.
 *
 * @p:	The pointer to the number buffer.
 *
 * Returns:
 *	The retrieved number
 */
static s32 uclogic_params_get_le24(const void *p)
{
	const __u8 *b = p;
	return b[0] | (b[1] << 8UL) | (b[2] << 16UL);
}

/**
 * uclogic_params_pen_init_v2() - initialize tablet interface pen
 * input and retrieve its parameters from the device, using v2 protocol.
 *
 * @pen:	Pointer to the pen parameters to initialize (to be
 *		cleaned up with uclogic_params_pen_cleanup()). Not modified in
 *		case of error, or if parameters are not found. Cannot be NULL.
 * @pfound:	Location for a flag which is set to true if the parameters
 *		were found, and to false if not (e.g. device was
 *		incompatible). Not modified in case of error. Cannot be NULL.
 * @hdev:	The HID device of the tablet interface to initialize and get
 *		parameters from. Cannot be NULL.
 *
 * Returns:
 *	Zero, if successful. A negative errno code on error.
 */
static int uclogic_params_pen_init_v2(struct uclogic_params_pen *pen,
					bool *pfound,
					struct hid_device *hdev)
{
	int rc;
	bool found = false;
	/* Buffer for (part of) the string descriptor */
	__u8 *buf = NULL;
	/* Descriptor length required */
	const int len = 18;
	s32 resolution;
	/* Pen report descriptor template parameters */
	s32 desc_params[UCLOGIC_RDESC_PEN_PH_ID_NUM];
	__u8 *desc_ptr = NULL;

	/* Check arguments */
	if (pen == NULL || pfound == NULL || hdev == NULL) {
		rc = -EINVAL;
		goto cleanup;
	}

	/*
	 * Read string descriptor containing pen input parameters.
	 * The specific string descriptor and data were discovered by sniffing
	 * the Windows driver traffic.
	 * NOTE: This enables fully-functional tablet mode.
	 */
	rc = uclogic_params_get_str_desc(&buf, hdev, 200, len);
	if (rc == -EPIPE) {
		hid_dbg(hdev,
			"string descriptor with pen parameters not found, assuming not compatible\n");
		goto finish;
	} else if (rc < 0) {
		hid_err(hdev, "failed retrieving pen parameters: %d\n", rc);
		goto cleanup;
	} else if (rc != len) {
		hid_dbg(hdev,
			"string descriptor with pen parameters has invalid length (got %d, expected %d), assuming not compatible\n",
			rc, len);
		goto finish;
	} else {
		size_t i;
		/*
		 * Check it's not just a catch-all UTF-16LE-encoded ASCII
		 * string (such as the model name) some tablets put into all
		 * unknown string descriptors.
		 */
		for (i = 2;
		     i < len &&
			(buf[i] >= 0x20 && buf[i] < 0x7f && buf[i + 1] == 0);
		     i += 2);
		if (i >= len) {
			hid_dbg(hdev,
				"string descriptor with pen parameters seems to contain only text, assuming not compatible\n");
			goto finish;
		}
	}

	/*
	 * Fill report descriptor parameters from the string descriptor
	 */
	desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_LM] =
		uclogic_params_get_le24(buf + 2);
	desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_LM] =
		uclogic_params_get_le24(buf + 5);
	desc_params[UCLOGIC_RDESC_PEN_PH_ID_PRESSURE_LM] =
		get_unaligned_le16(buf + 8);
	resolution = get_unaligned_le16(buf + 10);
	if (resolution == 0) {
		desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_PM] = 0;
		desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_PM] = 0;
	} else {
		desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_PM] =
			desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_LM] * 1000 /
			resolution;
		desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_PM] =
			desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_LM] * 1000 /
			resolution;
	}
	kfree(buf);
	buf = NULL;

	/*
	 * Generate pen report descriptor
	 */
	desc_ptr = uclogic_rdesc_template_apply(
				uclogic_rdesc_pen_v2_template_arr,
				uclogic_rdesc_pen_v2_template_size,
				desc_params, ARRAY_SIZE(desc_params));
	if (desc_ptr == NULL) {
		rc = -ENOMEM;
		goto cleanup;
	}

	/*
	 * Fill-in the parameters
	 */
	memset(pen, 0, sizeof(*pen));
	pen->desc_ptr = desc_ptr;
	desc_ptr = NULL;
	pen->desc_size = uclogic_rdesc_pen_v2_template_size;
	pen->id = UCLOGIC_RDESC_PEN_V2_ID;
	pen->inrange = UCLOGIC_PARAMS_PEN_INRANGE_NONE;
	pen->fragmented_hires = true;
	found = true;
finish:
	*pfound = found;
	rc = 0;
cleanup:
	kfree(desc_ptr);
	kfree(buf);
	return rc;
}

/**
 * uclogic_params_frame_cleanup - free resources used by struct
 * uclogic_params_frame (tablet interface's frame controls input parameters).
 * Can be called repeatedly.
 *
 * @frame:	Frame controls input parameters to cleanup. Cannot be NULL.
 */
static void uclogic_params_frame_cleanup(struct uclogic_params_frame *frame)
{
	kfree(frame->desc_ptr);
	memset(frame, 0, sizeof(*frame));
}

/**
 * uclogic_params_frame_init_with_desc() - initialize tablet's frame control
 * parameters with a static report descriptor.
 *
 * @frame:	Pointer to the frame parameters to initialize (to be cleaned
 *		up with uclogic_params_frame_cleanup()). Not modified in case
 *		of error. Cannot be NULL.
 * @desc_ptr:	Report descriptor pointer. Can be NULL, if desc_size is zero.
 * @desc_size:	Report descriptor size.
 * @id:		Report ID used for frame reports, if they should be tweaked,
 *		zero if not.
 *
 * Returns:
 *	Zero, if successful. A negative errno code on error.
 */
static int uclogic_params_frame_init_with_desc(
					struct uclogic_params_frame *frame,
					const __u8 *desc_ptr,
					size_t desc_size,
					unsigned int id)
{
	__u8 *copy_desc_ptr;

	if (frame == NULL || (desc_ptr == NULL && desc_size != 0))
		return -EINVAL;

	copy_desc_ptr = kmemdup(desc_ptr, desc_size, GFP_KERNEL);
	if (copy_desc_ptr == NULL)
		return -ENOMEM;

	memset(frame, 0, sizeof(*frame));
	frame->desc_ptr = copy_desc_ptr;
	frame->desc_size = desc_size;
	frame->id = id;
	return 0;
}

/**
 * uclogic_params_frame_init_v1_buttonpad() - initialize abstract buttonpad
 * on a v1 tablet interface.
 *
 * @frame:	Pointer to the frame parameters to initialize (to be cleaned
 *		up with uclogic_params_frame_cleanup()). Not modified in case
 *		of error, or if parameters are not found. Cannot be NULL.
 * @pfound:	Location for a flag which is set to true if the parameters
 *		were found, and to false if not (e.g. device was
 *		incompatible). Not modified in case of error. Cannot be NULL.
 * @hdev:	The HID device of the tablet interface to initialize and get
 *		parameters from. Cannot be NULL.
 *
 * Returns:
 *	Zero, if successful. A negative errno code on error.
 */
static int uclogic_params_frame_init_v1_buttonpad(
					struct uclogic_params_frame *frame,
					bool *pfound,
					struct hid_device *hdev)
{
	int rc;
	bool found = false;
	struct usb_device *usb_dev = hid_to_usb_dev(hdev);
	char *str_buf = NULL;
	const size_t str_len = 16;

	/* Check arguments */
	if (frame == NULL || pfound == NULL || hdev == NULL) {
		rc = -EINVAL;
		goto cleanup;
	}

	/*
	 * Enable generic button mode
	 */
	str_buf = kzalloc(str_len, GFP_KERNEL);
	if (str_buf == NULL) {
		rc = -ENOMEM;
		goto cleanup;
	}

	rc = usb_string(usb_dev, 123, str_buf, str_len);
	if (rc == -EPIPE) {
		hid_dbg(hdev,
			"generic button -enabling string descriptor not found\n");
	} else if (rc < 0) {
		goto cleanup;
	} else if (strncmp(str_buf, "HK On", rc) != 0) {
		hid_dbg(hdev,
			"invalid response to enabling generic buttons: \"%s\"\n",
			str_buf);
	} else {
		hid_dbg(hdev, "generic buttons enabled\n");
		rc = uclogic_params_frame_init_with_desc(
				frame,
				uclogic_rdesc_buttonpad_v1_arr,
				uclogic_rdesc_buttonpad_v1_size,
				UCLOGIC_RDESC_BUTTONPAD_V1_ID);
		if (rc != 0)
			goto cleanup;
		found = true;
	}

	*pfound = found;
	rc = 0;
cleanup:
	kfree(str_buf);
	return rc;
}

/**
 * uclogic_params_cleanup - free resources used by struct uclogic_params
 * (tablet interface's parameters).
 * Can be called repeatedly.
 *
 * @params:	Input parameters to cleanup. Cannot be NULL.
 */
void uclogic_params_cleanup(struct uclogic_params *params)
{
	if (!params->invalid) {
		kfree(params->desc_ptr);
		if (!params->pen_unused)
			uclogic_params_pen_cleanup(&params->pen);
		uclogic_params_frame_cleanup(&params->frame);
		memset(params, 0, sizeof(*params));
	}
}

/**
 * Get a replacement report descriptor for a tablet's interface.
 *
 * @params:	The parameters of a tablet interface to get report
 *		descriptor for. Cannot be NULL.
 * @pdesc:	Location for the resulting, kmalloc-allocated report
 *		descriptor pointer, or for NULL, if there's no replacement
 *		report descriptor. Not modified in case of error. Cannot be
 *		NULL.
 * @psize:	Location for the resulting report descriptor size, not set if
 *		there's no replacement report descriptor. Not modified in case
 *		of error. Cannot be NULL.
 *
 * Returns:
 *	Zero, if successful.
 *	-EINVAL, if invalid arguments are supplied.
 *	-ENOMEM, if failed to allocate memory.
 */
int uclogic_params_get_desc(const struct uclogic_params *params,
				__u8 **pdesc,
				unsigned int *psize)
{
	bool common_present;
	bool pen_present;
	bool frame_present;
	unsigned int size;
	__u8 *desc = NULL;

	/* Check arguments */
	if (params == NULL || pdesc == NULL || psize == NULL)
		return -EINVAL;

	size = 0;

	common_present = (params->desc_ptr != NULL);
	pen_present = (!params->pen_unused && params->pen.desc_ptr != NULL);
	frame_present = (params->frame.desc_ptr != NULL);

	if (common_present)
		size += params->desc_size;
	if (pen_present)
		size += params->pen.desc_size;
	if (frame_present)
		size += params->frame.desc_size;

	if (common_present || pen_present || frame_present) {
		__u8 *p;

		desc = kmalloc(size, GFP_KERNEL);
		if (desc == NULL)
			return -ENOMEM;
		p = desc;

		if (common_present) {
			memcpy(p, params->desc_ptr,
				params->desc_size);
			p += params->desc_size;
		}
		if (pen_present) {
			memcpy(p, params->pen.desc_ptr,
				params->pen.desc_size);
			p += params->pen.desc_size;
		}
		if (frame_present) {
			memcpy(p, params->frame.desc_ptr,
				params->frame.desc_size);
			p += params->frame.desc_size;
		}

		WARN_ON(p != desc + size);

		*psize = size;
	}

	*pdesc = desc;
	return 0;
}

/**
 * uclogic_params_init_invalid() - initialize tablet interface parameters,
 * specifying the interface is invalid.
 *
 * @params:		Parameters to initialize (to be cleaned with
 *			uclogic_params_cleanup()). Cannot be NULL.
 */
static void uclogic_params_init_invalid(struct uclogic_params *params)
{
	params->invalid = true;
}

/**
 * uclogic_params_init_with_opt_desc() - initialize tablet interface
 * parameters with an optional replacement report descriptor. Only modify
 * report descriptor, if the original report descriptor matches the expected
 * size.
 *
 * @params:		Parameters to initialize (to be cleaned with
 *			uclogic_params_cleanup()). Not modified in case of
 *			error. Cannot be NULL.
 * @hdev:		The HID device of the tablet interface create the
 *			parameters for. Cannot be NULL.
 * @orig_desc_size:	Expected size of the original report descriptor to
 *			be replaced.
 * @desc_ptr:		Pointer to the replacement report descriptor.
 *			Can be NULL, if desc_size is zero.
 * @desc_size:		Size of the replacement report descriptor.
 *
 * Returns:
 *	Zero, if successful. -EINVAL if an invalid argument was passed.
 *	-ENOMEM, if failed to allocate memory.
 */
static int uclogic_params_init_with_opt_desc(struct uclogic_params *params,
					     struct hid_device *hdev,
					     unsigned int orig_desc_size,
					     __u8 *desc_ptr,
					     unsigned int desc_size)
{
	__u8 *desc_copy_ptr = NULL;
	unsigned int desc_copy_size;
	int rc;

	/* Check arguments */
	if (params == NULL || hdev == NULL ||
	    (desc_ptr == NULL && desc_size != 0)) {
		rc = -EINVAL;
		goto cleanup;
	}

	/* Replace report descriptor, if it matches */
	if (hdev->dev_rsize == orig_desc_size) {
		hid_dbg(hdev,
			"device report descriptor matches the expected size, replacing\n");
		desc_copy_ptr = kmemdup(desc_ptr, desc_size, GFP_KERNEL);
		if (desc_copy_ptr == NULL) {
			rc = -ENOMEM;
			goto cleanup;
		}
		desc_copy_size = desc_size;
	} else {
		hid_dbg(hdev,
			"device report descriptor doesn't match the expected size (%u != %u), preserving\n",
			hdev->dev_rsize, orig_desc_size);
		desc_copy_ptr = NULL;
		desc_copy_size = 0;
	}

	/* Output parameters */
	memset(params, 0, sizeof(*params));
	params->desc_ptr = desc_copy_ptr;
	desc_copy_ptr = NULL;
	params->desc_size = desc_copy_size;

	rc = 0;
cleanup:
	kfree(desc_copy_ptr);
	return rc;
}

/**
 * uclogic_params_init_with_pen_unused() - initialize tablet interface
 * parameters preserving original reports and generic HID processing, but
 * disabling pen usage.
 *
 * @params:		Parameters to initialize (to be cleaned with
 *			uclogic_params_cleanup()). Not modified in case of
 *			error. Cannot be NULL.
 */
static void uclogic_params_init_with_pen_unused(struct uclogic_params *params)
{
	memset(params, 0, sizeof(*params));
	params->pen_unused = true;
}

/**
 * uclogic_params_init() - initialize a Huion tablet interface and discover
 * its parameters.
 *
 * @params:	Parameters to fill in (to be cleaned with
 *		uclogic_params_cleanup()). Not modified in case of error.
 *		Cannot be NULL.
 * @hdev:	The HID device of the tablet interface to initialize and get
 *		parameters from. Cannot be NULL.
 *
 * Returns:
 *	Zero, if successful. A negative errno code on error.
 */
static int uclogic_params_huion_init(struct uclogic_params *params,
				     struct hid_device *hdev)
{
	int rc;
	struct usb_device *udev = hid_to_usb_dev(hdev);
	struct usb_interface *iface = to_usb_interface(hdev->dev.parent);
	__u8 bInterfaceNumber = iface->cur_altsetting->desc.bInterfaceNumber;
	bool found;
	/* The resulting parameters (noop) */
	struct uclogic_params p = {0, };
	static const char transition_ver[] = "HUION_T153_160607";
	char *ver_ptr = NULL;
	const size_t ver_len = sizeof(transition_ver) + 1;

	/* Check arguments */
	if (params == NULL || hdev == NULL) {
		rc = -EINVAL;
		goto cleanup;
	}

	/* If it's not a pen interface */
	if (bInterfaceNumber != 0) {
		/* TODO: Consider marking the interface invalid */
		uclogic_params_init_with_pen_unused(&p);
		goto output;
	}

	/* Try to get firmware version */
	ver_ptr = kzalloc(ver_len, GFP_KERNEL);
	if (ver_ptr == NULL) {
		rc = -ENOMEM;
		goto cleanup;
	}
	rc = usb_string(udev, 201, ver_ptr, ver_len);
	if (rc == -EPIPE) {
		*ver_ptr = '\0';
	} else if (rc < 0) {
		hid_err(hdev,
			"failed retrieving Huion firmware version: %d\n", rc);
		goto cleanup;
	}

	/* If this is a transition firmware */
	if (strcmp(ver_ptr, transition_ver) == 0) {
		hid_dbg(hdev,
			"transition firmware detected, not probing pen v2 parameters\n");
	} else {
		/* Try to probe v2 pen parameters */
		rc = uclogic_params_pen_init_v2(&p.pen, &found, hdev);
		if (rc != 0) {
			hid_err(hdev,
				"failed probing pen v2 parameters: %d\n", rc);
			goto cleanup;
		} else if (found) {
			hid_dbg(hdev, "pen v2 parameters found\n");
			/* Create v2 buttonpad parameters */
			rc = uclogic_params_frame_init_with_desc(
					&p.frame,
					uclogic_rdesc_buttonpad_v2_arr,
					uclogic_rdesc_buttonpad_v2_size,
					UCLOGIC_RDESC_BUTTONPAD_V2_ID);
			if (rc != 0) {
				hid_err(hdev,
					"failed creating v2 buttonpad parameters: %d\n",
					rc);
				goto cleanup;
			}
			/* Set bitmask marking frame reports in pen reports */
			p.pen_frame_flag = 0x20;
			goto output;
		}
		hid_dbg(hdev, "pen v2 parameters not found\n");
	}

	/* Try to probe v1 pen parameters */
	rc = uclogic_params_pen_init_v1(&p.pen, &found, hdev);
	if (rc != 0) {
		hid_err(hdev,
			"failed probing pen v1 parameters: %d\n", rc);
		goto cleanup;
	} else if (found) {
		hid_dbg(hdev, "pen v1 parameters found\n");
		/* Try to probe v1 buttonpad */
		rc = uclogic_params_frame_init_v1_buttonpad(
						&p.frame,
						&found, hdev);
		if (rc != 0) {
			hid_err(hdev, "v1 buttonpad probing failed: %d\n", rc);
			goto cleanup;
		}
		hid_dbg(hdev, "buttonpad v1 parameters%s found\n",
			(found ? "" : " not"));
		if (found) {
			/* Set bitmask marking frame reports */
			p.pen_frame_flag = 0x20;
		}
		goto output;
	}
	hid_dbg(hdev, "pen v1 parameters not found\n");

	uclogic_params_init_invalid(&p);

output:
	/* Output parameters */
	memcpy(params, &p, sizeof(*params));
	memset(&p, 0, sizeof(p));
	rc = 0;
cleanup:
	kfree(ver_ptr);
	uclogic_params_cleanup(&p);
	return rc;
}

/**
 * uclogic_params_init() - initialize a tablet interface and discover its
 * parameters.
 *
 * @params:	Parameters to fill in (to be cleaned with
 *		uclogic_params_cleanup()). Not modified in case of error.
 *		Cannot be NULL.
 * @hdev:	The HID device of the tablet interface to initialize and get
 *		parameters from. Cannot be NULL. Must be using the USB low-level
 *		driver, i.e. be an actual USB tablet.
 *
 * Returns:
 *	Zero, if successful. A negative errno code on error.
 */
int uclogic_params_init(struct uclogic_params *params,
			struct hid_device *hdev)
{
	int rc;
	struct usb_device *udev = hid_to_usb_dev(hdev);
	__u8  bNumInterfaces = udev->config->desc.bNumInterfaces;
	struct usb_interface *iface = to_usb_interface(hdev->dev.parent);
	__u8 bInterfaceNumber = iface->cur_altsetting->desc.bInterfaceNumber;
	bool found;
	/* The resulting parameters (noop) */
	struct uclogic_params p = {0, };

	/* Check arguments */
	if (params == NULL || hdev == NULL ||
	    !hid_is_using_ll_driver(hdev, &usb_hid_driver)) {
		rc = -EINVAL;
		goto cleanup;
	}

	/*
	 * Set replacement report descriptor if the original matches the
	 * specified size. Otherwise keep interface unchanged.
	 */
#define WITH_OPT_DESC(_orig_desc_token, _new_desc_token) \
	uclogic_params_init_with_opt_desc(                  \
		&p, hdev,                                   \
		UCLOGIC_RDESC_##_orig_desc_token##_SIZE,    \
		uclogic_rdesc_##_new_desc_token##_arr,      \
		uclogic_rdesc_##_new_desc_token##_size)

#define VID_PID(_vid, _pid) \
	(((__u32)(_vid) << 16) | ((__u32)(_pid) & U16_MAX))

	/*
	 * Handle specific interfaces for specific tablets.
	 *
	 * Observe the following logic:
	 *
	 * If the interface is recognized as producing certain useful input:
	 *	Mark interface as valid.
	 *	Output interface parameters.
	 * Else, if the interface is recognized as *not* producing any useful
	 * input:
	 *	Mark interface as invalid.
	 * Else:
	 *	Mark interface as valid.
	 *	Output noop parameters.
	 *
	 * Rule of thumb: it is better to disable a broken interface than let
	 *		  it spew garbage input.
	 */

	switch (VID_PID(hdev->vendor, hdev->product)) {
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_TABLET_PF1209):
		rc = WITH_OPT_DESC(PF1209_ORIG, pf1209_fixed);
		if (rc != 0)
			goto cleanup;
		break;
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_TABLET_WP4030U):
		rc = WITH_OPT_DESC(WPXXXXU_ORIG, wp4030u_fixed);
		if (rc != 0)
			goto cleanup;
		break;
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_TABLET_WP5540U):
		if (hdev->dev_rsize == UCLOGIC_RDESC_WP5540U_V2_ORIG_SIZE) {
			if (bInterfaceNumber == 0) {
				/* Try to probe v1 pen parameters */
				rc = uclogic_params_pen_init_v1(&p.pen,
								&found, hdev);
				if (rc != 0) {
					hid_err(hdev,
						"pen probing failed: %d\n",
						rc);
					goto cleanup;
				}
				if (!found) {
					hid_warn(hdev,
						 "pen parameters not found");
				}
			} else {
				uclogic_params_init_invalid(&p);
			}
		} else {
			rc = WITH_OPT_DESC(WPXXXXU_ORIG, wp5540u_fixed);
			if (rc != 0)
				goto cleanup;
		}
		break;
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_TABLET_WP8060U):
		rc = WITH_OPT_DESC(WPXXXXU_ORIG, wp8060u_fixed);
		if (rc != 0)
			goto cleanup;
		break;
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_TABLET_WP1062):
		rc = WITH_OPT_DESC(WP1062_ORIG, wp1062_fixed);
		if (rc != 0)
			goto cleanup;
		break;
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_WIRELESS_TABLET_TWHL850):
		switch (bInterfaceNumber) {
		case 0:
			rc = WITH_OPT_DESC(TWHL850_ORIG0, twhl850_fixed0);
			if (rc != 0)
				goto cleanup;
			break;
		case 1:
			rc = WITH_OPT_DESC(TWHL850_ORIG1, twhl850_fixed1);
			if (rc != 0)
				goto cleanup;
			break;
		case 2:
			rc = WITH_OPT_DESC(TWHL850_ORIG2, twhl850_fixed2);
			if (rc != 0)
				goto cleanup;
			break;
		}
		break;
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_TABLET_TWHA60):
		/*
		 * If it is not a three-interface version, which is known to
		 * respond to initialization.
		 */
		if (bNumInterfaces != 3) {
			switch (bInterfaceNumber) {
			case 0:
				rc = WITH_OPT_DESC(TWHA60_ORIG0,
							twha60_fixed0);
				if (rc != 0)
					goto cleanup;
				break;
			case 1:
				rc = WITH_OPT_DESC(TWHA60_ORIG1,
							twha60_fixed1);
				if (rc != 0)
					goto cleanup;
				break;
			}
			break;
		}
		/* FALL THROUGH */
	case VID_PID(USB_VENDOR_ID_HUION,
		     USB_DEVICE_ID_HUION_TABLET):
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_HUION_TABLET):
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_YIYNOVA_TABLET):
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_UGEE_TABLET_81):
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_DRAWIMAGE_G3):
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_UGEE_TABLET_45):
	case VID_PID(USB_VENDOR_ID_UCLOGIC,
		     USB_DEVICE_ID_UCLOGIC_UGEE_TABLET_47):
		rc = uclogic_params_huion_init(&p, hdev);
		if (rc != 0)
			goto cleanup;
		break;
	case VID_PID(USB_VENDOR_ID_UGTIZER,
		     USB_DEVICE_ID_UGTIZER_TABLET_GP0610):
	case VID_PID(USB_VENDOR_ID_UGEE,
		     USB_DEVICE_ID_UGEE_XPPEN_TABLET_G540):
	case VID_PID(USB_VENDOR_ID_UGEE,
		     USB_DEVICE_ID_UGEE_XPPEN_TABLET_G640):
		/* If this is the pen interface */
		if (bInterfaceNumber == 1) {
			/* Probe v1 pen parameters */
			rc = uclogic_params_pen_init_v1(&p.pen, &found, hdev);
			if (rc != 0) {
				hid_err(hdev, "pen probing failed: %d\n", rc);
				goto cleanup;
			}
			if (!found) {
				hid_warn(hdev, "pen parameters not found");
				uclogic_params_init_invalid(&p);
			}
		} else {
			/* TODO: Consider marking the interface invalid */
			uclogic_params_init_with_pen_unused(&p);
		}
		break;
	case VID_PID(USB_VENDOR_ID_UGEE,
		     USB_DEVICE_ID_UGEE_XPPEN_TABLET_DECO01):
		/* If this is the pen and frame interface */
		if (bInterfaceNumber == 1) {
			/* Probe v1 pen parameters */
			rc = uclogic_params_pen_init_v1(&p.pen, &found, hdev);
			if (rc != 0) {
				hid_err(hdev, "pen probing failed: %d\n", rc);
				goto cleanup;
			}
			/* Initialize frame parameters */
			rc = uclogic_params_frame_init_with_desc(
				&p.frame,
				uclogic_rdesc_xppen_deco01_frame_arr,
				uclogic_rdesc_xppen_deco01_frame_size,
				0);
			if (rc != 0)
				goto cleanup;
		} else {
			/* TODO: Consider marking the interface invalid */
			uclogic_params_init_with_pen_unused(&p);
		}
		break;
	case VID_PID(USB_VENDOR_ID_UGEE,
		     USB_DEVICE_ID_UGEE_TABLET_G5):
		/* Ignore non-pen interfaces */
		if (bInterfaceNumber != 1) {
			uclogic_params_init_invalid(&p);
			break;
		}

		rc = uclogic_params_pen_init_v1(&p.pen, &found, hdev);
		if (rc != 0) {
			hid_err(hdev, "pen probing failed: %d\n", rc);
			goto cleanup;
		} else if (found) {
			rc = uclogic_params_frame_init_with_desc(
				&p.frame,
				uclogic_rdesc_ugee_g5_frame_arr,
				uclogic_rdesc_ugee_g5_frame_size,
				UCLOGIC_RDESC_UGEE_G5_FRAME_ID);
			if (rc != 0) {
				hid_err(hdev,
					"failed creating buttonpad parameters: %d\n",
					rc);
				goto cleanup;
			}
			p.frame.re_lsb =
				UCLOGIC_RDESC_UGEE_G5_FRAME_RE_LSB;
			p.frame.dev_id_byte =
				UCLOGIC_RDESC_UGEE_G5_FRAME_DEV_ID_BYTE;
		} else {
			hid_warn(hdev, "pen parameters not found");
			uclogic_params_init_invalid(&p);
		}

		break;
	case VID_PID(USB_VENDOR_ID_UGEE,
		     USB_DEVICE_ID_UGEE_TABLET_EX07S):
		/* Ignore non-pen interfaces */
		if (bInterfaceNumber != 1) {
			uclogic_params_init_invalid(&p);
			break;
		}

		rc = uclogic_params_pen_init_v1(&p.pen, &found, hdev);
		if (rc != 0) {
			hid_err(hdev, "pen probing failed: %d\n", rc);
			goto cleanup;
		} else if (found) {
			rc = uclogic_params_frame_init_with_desc(
				&p.frame,
				uclogic_rdesc_ugee_ex07_buttonpad_arr,
				uclogic_rdesc_ugee_ex07_buttonpad_size,
				0);
			if (rc != 0) {
				hid_err(hdev,
					"failed creating buttonpad parameters: %d\n",
					rc);
				goto cleanup;
			}
		} else {
			hid_warn(hdev, "pen parameters not found");
			uclogic_params_init_invalid(&p);
		}

		break;
	}

#undef VID_PID
#undef WITH_OPT_DESC

	/* Output parameters */
	memcpy(params, &p, sizeof(*params));
	memset(&p, 0, sizeof(p));
	rc = 0;
cleanup:
	uclogic_params_cleanup(&p);
	return rc;
}
