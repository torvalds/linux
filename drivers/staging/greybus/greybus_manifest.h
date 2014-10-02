/*
 * Greybus module manifest definition
 *
 * See "Greybus Application Protocol" document (version 0.1) for
 * details on these values and structures.
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __GREYBUS_MANIFEST_H
#define __GREYBUS_MANIFEST_H

#pragma pack(push, 1)

enum greybus_descriptor_type {
	GREYBUS_TYPE_INVALID		= 0x00,
	GREYBUS_TYPE_MODULE		= 0x01,
	GREYBUS_TYPE_DEVICE		= 0x02,
	GREYBUS_TYPE_CLASS		= 0x03,
	GREYBUS_TYPE_STRING		= 0x04,
	GREYBUS_TYPE_CPORT		= 0x05,
};

enum greybus_function_type {
	GREYBUS_FUNCTION_CONTROL	= 0x00,
	GREYBUS_FUNCTION_USB		= 0x01,
	GREYBUS_FUNCTION_GPIO		= 0x02,
	GREYBUS_FUNCTION_SPI		= 0x03,
	GREYBUS_FUNCTION_UART		= 0x04,
	GREYBUS_FUNCTION_PWM		= 0x05,
	GREYBUS_FUNCTION_I2S		= 0x06,
	GREYBUS_FUNCTION_I2C		= 0x07,
	GREYBUS_FUNCTION_SDIO		= 0x08,
	GREYBUS_FUNCTION_HID		= 0x09,
	GREYBUS_FUNCTION_DISPLAY	= 0x0a,
	GREYBUS_FUNCTION_CAMERA		= 0x0b,
	GREYBUS_FUNCTION_SENSOR		= 0x0c,
	GREYBUS_FUNCTION_VENDOR		= 0xff,
};

/*
 * A module descriptor describes information about a module as a
 * whole, *not* the functions within it.
 */
struct greybus_descriptor_module {
	__le16	vendor;
	__le16	product;
	__le16	version;
	__le64	serial_number;
	__u8	vendor_stringid;
	__u8	product_stringid;
};

/*
 * A UniPro device normally supports a range of 32 CPorts (0..31).
 * It is possible to support more than this by having a UniPro
 * switch treat one device as if it were more than one.  E.g.,
 * allocate 3 device ids (rather than the normal--1) to physical
 * device 5, and configure the switch to route all packets destined
 * for "encoded" device ids 5, 6, and 7 to physical device 5.
 * Device 5 uses the encoded device id in incoming UniPro packets to
 * determine which bank of 32 CPorts should receive the UniPro
 * segment.
 *
 * The "scale" field in this structure is used to define the number
 * of encoded device ids should be allocated for this physical
 * device.  Scale is normally 1, to represent 32 available CPorts.
 * A scale value 2 represents up to 64 CPorts; scale value 3
 * represents up to 96 CPorts, and so on.
 */
struct greybus_descriptor_interface {
	__u8	id;	/* module-relative id (0..) */
	__u8	scale;	/* indicates range of of CPorts supported */
	/* UniPro gear, number of in/out lanes */
};

struct greybus_descriptor_cport {
	__le16	id;
	__u8	function_type;	/* enum greybus_function_type */
};

struct greybus_descriptor_string {
	__u8	length;
	__u8	id;
	__u8	string[0];
};

struct greybus_descriptor_header {
	__le16	size;
	__u8	type;	/* enum greybus_descriptor_type */
};

struct greybus_descriptor {
	struct greybus_descriptor_header	header;
	union {
		struct greybus_descriptor_module	module;
		struct greybus_descriptor_string	string;
		struct greybus_descriptor_interface	interface;
		struct greybus_descriptor_cport		cport;
	};
};

struct greybus_manifest_header {
	__le16	size;
	__u8	version_major;
	__u8	version_minor;
};

struct greybus_manifest {
	struct greybus_manifest_header		header;
	struct greybus_descriptor		descriptors[0];
};

#pragma pack(pop)

#endif /* __GREYBUS_MANIFEST_H */
