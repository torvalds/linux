/*
 * Greybus device descriptor definition
 *
 * Defined in the "Greybus Application Protocol" document.
 * See that document for any details on these values and structures.
 *
 * Copyright 2014 Google Inc.
 */

#ifndef __GREYBUS_DESC_H
#define __GREYBUS_DESC_H

struct greybus_decriptor_block_header {
	__le16	size;
	u8	version_major;
	u8	version_minor;
};

enum greybus_descriptor_type {
	GREYBUS_TYPE_INVALID		= 0x0000,
	GREYBUS_TYPE_DEVICE_ID		= 0x0001,
	GREYBUS_TYPE_SERIAL_NUMBER	= 0x0002,
	GREYBUS_TYPE_DEVICE_STRING	= 0x0003,
	GREYBUS_TYPE_CPORT		= 0x0004,
	GREYBUS_TYPE_FUNCTION		= 0x0005,
};

struct greybus_descriptor_header {
	__le16	size;
	__le16	type;	/* enum greybus_descriptor_type */
};


struct greybus_descriptor_deviceid {
	__le16	vendor;
	__le16	product;
	__le16	version;
	u8	vendor_stringid;
	u8	product_stringid;
};

struct greybus_descriptor_serial_number {
	__le64	serial_number;
};

struct greybus_descriptor_string {
	u8	id;
	__le16	length;
	u8	string[0];
};

struct greybus_descriptor_cport {
	__le16	number;
	u8	speed;	// FIXME
	u8	reserved;
};

enum greybus_function_class {
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
	__le16	number;
	__le16	cport;
	u8	function_class;		/* enum greybus_function_class */
	u8	function_subclass;
	u8	function_protocol;
	u8	reserved;
};

struct greybus_msg_descriptor {
	struct greybus_descriptor_header	header;
	union {
		struct greybus_descriptor_deviceid	device_id;
		struct greybus_descriptor_serial_number	serial_number;
		struct greybus_descriptor_string	string;
		struct greybus_descriptor_cport		cport;
		struct greybus_descriptor_function	function;
	};
};

#endif /* __GREYBUS_DESC_H */
