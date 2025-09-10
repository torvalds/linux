/* SPDX-License-Identifier: GPL-2.0+ */
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

#ifndef _HID_UCLOGIC_PARAMS_H
#define _HID_UCLOGIC_PARAMS_H

#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/list.h>

#define UCLOGIC_MOUSE_FRAME_QUIRK	BIT(0)
#define UCLOGIC_BATTERY_QUIRK		BIT(1)

/* Types of pen in-range reporting */
enum uclogic_params_pen_inrange {
	/* Normal reports: zero - out of proximity, one - in proximity */
	UCLOGIC_PARAMS_PEN_INRANGE_NORMAL = 0,
	/* Inverted reports: zero - in proximity, one - out of proximity */
	UCLOGIC_PARAMS_PEN_INRANGE_INVERTED,
	/* No reports */
	UCLOGIC_PARAMS_PEN_INRANGE_NONE,
};

/* Types of frames */
enum uclogic_params_frame_type {
	/* Frame with buttons */
	UCLOGIC_PARAMS_FRAME_BUTTONS = 0,
	/* Frame with buttons and a dial */
	UCLOGIC_PARAMS_FRAME_DIAL,
	/* Frame with buttons and a mouse (shaped as a dial + touchpad) */
	UCLOGIC_PARAMS_FRAME_MOUSE,
};

/*
 * Pen report's subreport data.
 */
struct uclogic_params_pen_subreport {
	/*
	 * The value of the second byte of the pen report indicating this
	 * subreport. If zero, the subreport should be considered invalid and
	 * not matched.
	 */
	__u8 value;

	/*
	 * The ID to be assigned to the report, if the second byte of the pen
	 * report is equal to "value". Only valid if "value" is not zero.
	 */
	__u8 id;
};

/*
 * Tablet interface's pen input parameters.
 *
 * Must use declarative (descriptive) language, not imperative, to simplify
 * understanding and maintain consistency.
 *
 * Noop (preserving functionality) when filled with zeroes.
 */
struct uclogic_params_pen {
	/*
	 * True if pen usage is invalid for this interface and should be
	 * ignored, false otherwise.
	 */
	bool usage_invalid;
	/*
	 * Pointer to report descriptor part describing the pen inputs.
	 * Allocated with kmalloc. NULL if the part is not specified.
	 */
	const __u8 *desc_ptr;
	/*
	 * Size of the report descriptor.
	 * Only valid, if "desc_ptr" is not NULL.
	 */
	unsigned int desc_size;
	/* Report ID, if reports should be tweaked, zero if not */
	unsigned int id;
	/* The list of subreports, only valid if "id" is not zero */
	struct uclogic_params_pen_subreport subreport_list[3];
	/* Type of in-range reporting, only valid if "id" is not zero */
	enum uclogic_params_pen_inrange inrange;
	/*
	 * True, if reports include fragmented high resolution coords, with
	 * high-order X and then Y bytes following the pressure field.
	 * Only valid if "id" is not zero.
	 */
	bool fragmented_hires;
	/*
	 * True if the pen reports tilt in bytes at offset 10 (X) and 11 (Y),
	 * and the Y tilt direction is flipped.
	 * Only valid if "id" is not zero.
	 */
	bool tilt_y_flipped;
};

/*
 * Parameters of frame control inputs of a tablet interface.
 *
 * Must use declarative (descriptive) language, not imperative, to simplify
 * understanding and maintain consistency.
 *
 * Noop (preserving functionality) when filled with zeroes.
 */
struct uclogic_params_frame {
	/*
	 * Pointer to report descriptor part describing the frame inputs.
	 * Allocated with kmalloc. NULL if the part is not specified.
	 */
	const __u8 *desc_ptr;
	/*
	 * Size of the report descriptor.
	 * Only valid, if "desc_ptr" is not NULL.
	 */
	unsigned int desc_size;
	/*
	 * Report ID, if reports should be tweaked, zero if not.
	 */
	unsigned int id;
	/*
	 * The suffix to add to the input device name, if not NULL.
	 */
	const char *suffix;
	/*
	 * Number of the least-significant bit of the 2-bit state of a rotary
	 * encoder, in the report. Cannot point to a 2-bit field crossing a
	 * byte boundary. Zero if not present. Only valid if "id" is not zero.
	 */
	unsigned int re_lsb;
	/*
	 * Offset of the Wacom-style device ID byte in the report, to be set
	 * to pad device ID (0xf), for compatibility with Wacom drivers. Zero
	 * if no changes to the report should be made. The ID byte will be set
	 * to zero whenever the byte pointed by "touch_byte" is zero, if
	 * the latter is valid. Only valid if "id" is not zero.
	 */
	unsigned int dev_id_byte;
	/*
	 * Offset of the touch ring/strip state byte, in the report.
	 * Zero if not present. If dev_id_byte is also valid and non-zero,
	 * then the device ID byte will be cleared when the byte pointed to by
	 * this offset is zero. Only valid if "id" is not zero.
	 */
	unsigned int touch_byte;
	/*
	 * The value to anchor the reversed touch ring/strip reports at.
	 * I.e. one, if the reports should be flipped without offset.
	 * Zero if no reversal should be done.
	 * Only valid if "touch_byte" is valid and not zero.
	 */
	__s8 touch_flip_at;
	/*
	 * Maximum value of the touch ring/strip report around which the value
	 * should be wrapped when flipping according to "touch_flip_at".
	 * The minimum valid value is considered to be one, with zero being
	 * out-of-proximity (finger lift) value.
	 * Only valid if "touch_flip_at" is valid and not zero.
	 */
	__s8 touch_max;
	/*
	 * Offset of the bitmap dial byte, in the report. Zero if not present.
	 * Only valid if "id" is not zero. A bitmap dial sends reports with a
	 * dedicated bit per direction: 1 means clockwise rotation, 2 means
	 * counterclockwise, as opposed to the normal 1 and -1.
	 */
	unsigned int bitmap_dial_byte;
	/*
	 * Destination offset for the second bitmap dial byte, if the tablet
	 * supports a second dial at all.
	 */
	unsigned int bitmap_second_dial_destination_byte;
};

/*
 * List of works to be performed when a certain raw event is received.
 */
struct uclogic_raw_event_hook {
	struct hid_device *hdev;
	__u8 *event;
	size_t size;
	struct work_struct work;
	struct list_head list;
};

/*
 * Tablet interface report parameters.
 *
 * Must use declarative (descriptive) language, not imperative, to simplify
 * understanding and maintain consistency.
 *
 * When filled with zeros represents a "noop" configuration - passes all
 * reports unchanged and lets the generic HID driver handle everything.
 *
 * The resulting device report descriptor is assembled from all the report
 * descriptor parts referenced by the structure. No order of assembly should
 * be assumed. The structure represents original device report descriptor if
 * all the parts are NULL.
 */
struct uclogic_params {
	/*
	 * True if the whole interface is invalid, false otherwise.
	 */
	bool invalid;
	/*
	 * Pointer to the common part of the replacement report descriptor,
	 * allocated with kmalloc. NULL if no common part is needed.
	 * Only valid, if "invalid" is false.
	 */
	const __u8 *desc_ptr;
	/*
	 * Size of the common part of the replacement report descriptor.
	 * Only valid, if "desc_ptr" is valid and not NULL.
	 */
	unsigned int desc_size;
	/*
	 * Pen parameters and optional report descriptor part.
	 * Only valid, if "invalid" is false.
	 */
	struct uclogic_params_pen pen;
	/*
	 * The list of frame control parameters and optional report descriptor
	 * parts. Only valid, if "invalid" is false.
	 */
	struct uclogic_params_frame frame_list[3];
	/*
	 * List of event hooks.
	 */
	struct uclogic_raw_event_hook *event_hooks;
};

/* Driver data */
struct uclogic_drvdata {
	/* Interface parameters */
	struct uclogic_params params;
	/* Pointer to the replacement report descriptor. NULL if none. */
	const __u8 *desc_ptr;
	/*
	 * Size of the replacement report descriptor.
	 * Only valid if desc_ptr is not NULL
	 */
	unsigned int desc_size;
	/* Pen input device */
	struct input_dev *pen_input;
	/* In-range timer */
	struct timer_list inrange_timer;
	/* Last rotary encoder state, or U8_MAX for none */
	u8 re_state;
	/* Device quirks */
	unsigned long quirks;
};

/* Initialize a tablet interface and discover its parameters */
extern int uclogic_params_init(struct uclogic_params *params,
				struct hid_device *hdev);

/* Get a replacement report descriptor for a tablet's interface. */
extern int uclogic_params_get_desc(const struct uclogic_params *params,
					const __u8 **pdesc,
					unsigned int *psize);

/* Free resources used by tablet interface's parameters */
extern void uclogic_params_cleanup(struct uclogic_params *params);

/* Dump tablet interface parameters with hid_dbg() */
extern void uclogic_params_hid_dbg(const struct hid_device *hdev,
					const struct uclogic_params *params);

#endif /* _HID_UCLOGIC_PARAMS_H */
