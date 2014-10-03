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
	GREYBUS_TYPE_STRING		= 0x02,
	GREYBUS_TYPE_INTERFACE		= 0x03,
	GREYBUS_TYPE_CPORT		= 0x04,
	GREYBUS_TYPE_CLASS		= 0x05,
};

enum greybus_protocol {
	GREYBUS_PROTOCOL_CONTROL	= 0x00,
	GREYBUS_PROTOCOL_AP		= 0x01,
	GREYBUS_PROTOCOL_GPIO		= 0x02,
	GREYBUS_PROTOCOL_I2C		= 0x03,
	GREYBUS_PROTOCOL_UART		= 0x04,
	GREYBUS_PROTOCOL_HID		= 0x05,
		/* ... */
	GREYBUS_PROTOCOL_VENDOR		= 0xff,
};

enum greybus_class_type {
	GREYBUS_CLASS_CONTROL		= 0x00,
	GREYBUS_CLASS_USB		= 0x01,
	GREYBUS_CLASS_GPIO		= 0x02,
	GREYBUS_CLASS_SPI		= 0x03,
	GREYBUS_CLASS_UART		= 0x04,
	GREYBUS_CLASS_PWM		= 0x05,
	GREYBUS_CLASS_I2S		= 0x06,
	GREYBUS_CLASS_I2C		= 0x07,
	GREYBUS_CLASS_SDIO		= 0x08,
	GREYBUS_CLASS_HID		= 0x09,
	GREYBUS_CLASS_DISPLAY		= 0x0a,
	GREYBUS_CLASS_CAMERA		= 0x0b,
	GREYBUS_CLASS_SENSOR		= 0x0c,
	GREYBUS_CLASS_VENDOR		= 0xff,
};

/*
 * A module descriptor describes information about a module as a
 * whole, *not* the functions within it.
 */
struct greybus_descriptor_module {
	__le16	vendor;
	__le16	product;
	__le16	version;
	__u8	vendor_stringid;
	__u8	product_stringid;
	__le64	unique_id;
};

/*
 * The string in a string descriptor is not NUL-terminated.  The
 * size of the descriptor will be rounded up to a multiple of 4
 * bytes, by padding the string with 0x00 bytes if necessary.
 */
struct greybus_descriptor_string {
	__u8	length;
	__u8	id;
	__u8	string[0];
};

/*
 * An interface descriptor simply defines a module-unique id for
 * each interface present on a module.  Its sole purpose is to allow
 * CPort descriptors to specify which interface they are associated
 * with.  Normally there's only one interface, with id 0.  The
 * second one must have id 1, and so on consecutively.
 *
 * The largest CPort id associated with an interface (defined by a
 * CPort descriptor in the manifest) is used to determine how to
 * encode the device id and module number in UniPro packets
 * that use the interface.
 */
struct greybus_descriptor_interface {
	__u8	id;	/* module-relative id (0..) */
};

/*
 * A CPort descriptor indicates the id of the interface within the
 * module it's associated with, along with the CPort id used to
 * address the CPort.  The protocol defines the format of messages
 * exchanged using the CPort.
 */
struct greybus_descriptor_cport {
	__u8	interface;
	__le16	id;
	__u8	protocol;	/* enum greybus_protocol */
};

/*
 * A class descriptor defines functionality supplied by a module.
 * Beyond that, not much else is defined yet...
 */
struct greybus_descriptor_class {
	__u8	class;		/* enum greybus_class_type */
};

struct greybus_descriptor_header {
	__le16	size;
	__u8	type;		/* enum greybus_descriptor_type */
};

struct greybus_descriptor {
	struct greybus_descriptor_header		header;
	union {
		struct greybus_descriptor_module	module;
		struct greybus_descriptor_string	string;
		struct greybus_descriptor_interface	interface;
		struct greybus_descriptor_cport		cport;
		struct greybus_descriptor_class		class;
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
