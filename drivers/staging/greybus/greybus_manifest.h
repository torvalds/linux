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

#ifndef __GREYBUS_DESC_H
#define __GREYBUS_DESC_H

#pragma pack(push, 1)

struct greybus_manifest_header {
	__le16	size;
	__u8	version_major;
	__u8	version_minor;
};

enum greybus_descriptor_type {
	GREYBUS_TYPE_INVALID		= 0x0000,
	GREYBUS_TYPE_MODULE		= 0x0001,
	GREYBUS_TYPE_FUNCTION		= 0x0002,
	GREYBUS_TYPE_CLASS		= 0x0003,
	GREYBUS_TYPE_STRING		= 0x0004,
	GREYBUS_TYPE_CPORT		= 0x0005,
};

struct greybus_descriptor_header {
	__le16	size;
	__le16	type;	/* enum greybus_descriptor_type */
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

struct greybus_descriptor_function {
	__le16	cport;
	__u8	function_type;	/* enum greybus_function_type */
};

struct greybus_descriptor_module {
	__le16	vendor;
	__le16	product;
	__le16	version;
	__le64	serial_number;
	__u8	vendor_stringid;
	__u8	product_stringid;
};

struct greybus_descriptor_string {
	__u8	length;
	__u8	id;
	__u8	string[0];
};

struct greybus_descriptor_cport {
	__le16	id;
	__le16	size;
	__u8	speed;	// FIXME
	__u8	reserved;
};

struct greybus_descriptor {
	struct greybus_descriptor_header	header;
	union {
		struct greybus_descriptor_function	function;
		struct greybus_descriptor_module	module;
		struct greybus_descriptor_string	string;
		struct greybus_descriptor_cport		cport;
	};
};

struct greybus_manifest {
	struct greybus_manifest_header		header;
	struct greybus_descriptor		descriptors[0];
};

#pragma pack(pop)

#endif /* __GREYBUS_DESC_H */
