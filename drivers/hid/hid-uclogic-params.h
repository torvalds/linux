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

/* Types of pen in-range reporting */
enum uclogic_params_pen_inrange {
	/* Normal reports: zero - out of proximity, one - in proximity */
	UCLOGIC_PARAMS_PEN_INRANGE_NORMAL = 0,
	/* Inverted reports: zero - in proximity, one - out of proximity */
	UCLOGIC_PARAMS_PEN_INRANGE_INVERTED,
	/* No reports */
	UCLOGIC_PARAMS_PEN_INRANGE_NONE,
};

/* Convert a pen in-range reporting type to a string */
extern const char *uclogic_params_pen_inrange_to_str(
			enum uclogic_params_pen_inrange inrange);

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
	 * Pointer to report descriptor describing the inputs.
	 * Allocated with kmalloc.
	 */
	__u8 *desc_ptr;
	/*
	 * Size of the report descriptor.
	 * Only valid, if "desc_ptr" is not NULL.
	 */
	unsigned int desc_size;
	/* Report ID, if reports should be tweaked, zero if not */
	unsigned int id;
	/* Type of in-range reporting, only valid if "id" is not zero */
	enum uclogic_params_pen_inrange inrange;
	/*
	 * True, if reports include fragmented high resolution coords, with
	 * high-order X and then Y bytes following the pressure field.
	 * Only valid if "id" is not zero.
	 */
	bool fragmented_hires;
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
	 * Pointer to report descriptor describing the inputs.
	 * Allocated with kmalloc.
	 */
	__u8 *desc_ptr;
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
	 * Number of the least-significant bit of the 2-bit state of a rotary
	 * encoder, in the report. Cannot point to a 2-bit field crossing a
	 * byte boundary. Zero if not present. Only valid if "id" is not zero.
	 */
	unsigned int re_lsb;
	/*
	 * Offset of the Wacom-style device ID byte in the report, to be set
	 * to pad device ID (0xf), for compatibility with Wacom drivers. Zero
	 * if no changes to the report should be made. Only valid if "id" is
	 * not zero.
	 */
	unsigned int dev_id_byte;
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
	__u8 *desc_ptr;
	/*
	 * Size of the common part of the replacement report descriptor.
	 * Only valid, if "desc_ptr" is not NULL.
	 */
	unsigned int desc_size;
	/*
	 * True, if pen usage in report descriptor is invalid, when present.
	 * Only valid, if "invalid" is false.
	 */
	bool pen_unused;
	/*
	 * Pen parameters and optional report descriptor part.
	 * Only valid if "pen_unused" is valid and false.
	 */
	struct uclogic_params_pen pen;
	/*
	 * Frame control parameters and optional report descriptor part.
	 * Only valid, if "invalid" is false.
	 */
	struct uclogic_params_frame frame;
	/*
	 * Bitmask matching frame controls "sub-report" flag in the second
	 * byte of the pen report, or zero if it's not expected.
	 * Only valid if both "pen" and "frame" are valid, and "frame.id" is
	 * not zero.
	 */
	__u8 pen_frame_flag;
};

/* Initialize a tablet interface and discover its parameters */
extern int uclogic_params_init(struct uclogic_params *params,
				struct hid_device *hdev);

/* Tablet interface parameters *printf format string */
#define UCLOGIC_PARAMS_FMT_STR \
		".invalid = %s\n"                   \
		".desc_ptr = %p\n"                  \
		".desc_size = %u\n"                 \
		".pen_unused = %s\n"                \
		".pen.desc_ptr = %p\n"              \
		".pen.desc_size = %u\n"             \
		".pen.id = %u\n"                    \
		".pen.inrange = %s\n"               \
		".pen.fragmented_hires = %s\n"      \
		".frame.desc_ptr = %p\n"            \
		".frame.desc_size = %u\n"           \
		".frame.id = %u\n"                  \
		".frame.re_lsb = %u\n"              \
		".frame.dev_id_byte = %u\n"         \
		".pen_frame_flag = 0x%02x\n"

/* Tablet interface parameters *printf format arguments */
#define UCLOGIC_PARAMS_FMT_ARGS(_params) \
		((_params)->invalid ? "true" : "false"),                    \
		(_params)->desc_ptr,                                        \
		(_params)->desc_size,                                       \
		((_params)->pen_unused ? "true" : "false"),                 \
		(_params)->pen.desc_ptr,                                    \
		(_params)->pen.desc_size,                                   \
		(_params)->pen.id,                                          \
		uclogic_params_pen_inrange_to_str((_params)->pen.inrange),  \
		((_params)->pen.fragmented_hires ? "true" : "false"),       \
		(_params)->frame.desc_ptr,                                  \
		(_params)->frame.desc_size,                                 \
		(_params)->frame.id,                                        \
		(_params)->frame.re_lsb,                                    \
		(_params)->frame.dev_id_byte,                               \
		(_params)->pen_frame_flag

/* Get a replacement report descriptor for a tablet's interface. */
extern int uclogic_params_get_desc(const struct uclogic_params *params,
					__u8 **pdesc,
					unsigned int *psize);

/* Free resources used by tablet interface's parameters */
extern void uclogic_params_cleanup(struct uclogic_params *params);

#endif /* _HID_UCLOGIC_PARAMS_H */
