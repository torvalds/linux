/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Benjamin Tissoires
 */

#ifndef __HID_REPORT_DESCRIPTOR_HELPERS_H
#define __HID_REPORT_DESCRIPTOR_HELPERS_H

#include "vmlinux.h"

/* Compiler attributes */
#ifndef __packed
#define __packed __attribute__((packed))
#endif

#ifndef __maybe_unused
#define __maybe_unused __attribute__((__unused__))
#endif

/* Report Descriptor Structures */
#define HID_MAX_COLLECTIONS 32
#define HID_MAX_FIELDS 64
#define HID_MAX_REPORTS 16

enum hid_rdesc_field_type {
	HID_FIELD_VARIABLE = 0,
	HID_FIELD_ARRAY = 1,
	HID_FIELD_CONSTANT = 2,
};

struct hid_rdesc_collection {
	__u16 usage_page;
	__u16 usage_id;
	__u8 collection_type;
} __packed;

struct hid_rdesc_field {
	__u8 field_type;        /* enum hid_rdesc_field_type */
	__u8 num_collections;
	__u16 bits_start;
	__u16 bits_end;
	__u16 usage_page;
	union {
		__u16 usage_id;       /* For Variable fields */
		struct __packed {     /* For Array fields */
			__u16 usage_minimum;
			__u16 usage_maximum;
		};
	};
	__s32 logical_minimum;
	__s32 logical_maximum;
	struct {
		__u8 is_relative:1;             /* Data is relative to previous value */
		__u8 wraps:1;                   /* Value wraps around (e.g., rotary encoder) */
		__u8 is_nonlinear:1;            /* Non-linear relationship between logical/physical */
		__u8 has_no_preferred_state:1;  /* No rest position (e.g., free-floating joystick) */
		__u8 has_null_state:1;          /* Can report null/no-data values */
		__u8 is_volatile:1;             /* Volatile (Output/Feature) - NOT POPULATED, always 0 */
		__u8 is_buffered_bytes:1;       /* Fixed-size byte stream vs bitfield */
		__u8 reserved:1;                /* Reserved for future use */
	} flags;
	struct hid_rdesc_collection collections[HID_MAX_COLLECTIONS];
} __packed;

struct hid_rdesc_report {
	__u8 report_id;         /* 0 means no report ID */
	__u16 size_in_bits;
	__u8 num_fields;
	struct hid_rdesc_field fields[HID_MAX_FIELDS];
} __packed;

struct hid_rdesc_descriptor {
	__u8 num_input_reports;
	__u8 num_output_reports;
	__u8 num_feature_reports;
	struct hid_rdesc_report input_reports[HID_MAX_REPORTS];
	struct hid_rdesc_report output_reports[HID_MAX_REPORTS];
	struct hid_rdesc_report feature_reports[HID_MAX_REPORTS];
} __packed;

#endif /* __HID_REPORT_DESCRIPTOR_HELPERS_H */
