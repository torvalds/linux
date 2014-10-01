/*
 * Greybus AP <-> SVC message structure format.
 *
 * See "Greybus Application Protocol" document (version 0.1) for
 * details on these values and structures.
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __SVC_MSG_H
#define __SVC_MSG_H

#pragma pack(push, 1)

enum svc_function_id {
	SVC_FUNCTION_HANDSHAKE			= 0x00,
	SVC_FUNCTION_UNIPRO_NETWORK_MANAGEMENT	= 0x01,
	SVC_FUNCTION_HOTPLUG			= 0x02,
	SVC_FUNCTION_DDB			= 0x03,
	SVC_FUNCTION_POWER			= 0x04,
	SVC_FUNCTION_EPM			= 0x05,
	SVC_FUNCTION_SUSPEND			= 0x06,
};

enum svc_msg_type {
	SVC_MSG_DATA				= 0x00,
	SVC_MSG_ERROR				= 0xff,
};

struct svc_msg_header {
	__u8	function_id;	/* enum svc_function_id */
	__u8	message_type;
	__le16	payload_length;
};

enum svc_function_handshake_type {
	SVC_HANDSHAKE_SVC_HELLO		= 0x00,
	SVC_HANDSHAKE_AP_HELLO		= 0x01,
	SVC_HANDSHAKE_MODULE_HELLO	= 0x02,
};

struct svc_function_handshake {
	__u8	version_major;
	__u8	version_minor;
	__u8	handshake_type;	/* enum svc_function_handshake_type */
};

struct svc_function_unipro_set_route {
	__u8	source_device_id;
	__u8	source_cport_id;	/* bottom 8 bits */
	__u8	destination_device_id;
	__u8	destination_cport_id;	/* bottom 8 bits */
};

struct svc_function_unipro_link_up {
	__u8	device_id;
};

enum svc_function_management_event {
	SVC_MANAGEMENT_SET_ROUTE	= 0x00,
	SVC_MANAGEMENT_LINK_UP		= 0x01,
};

struct svc_function_unipro_management {
	__u8	management_packet_type;	/* enum svc_function_management_event */
	union {
		struct svc_function_unipro_set_route	set_route;
		struct svc_function_unipro_link_up	link_up;
	};
};

enum svc_function_hotplug_event {
	SVC_HOTPLUG_EVENT	= 0x00,
	SVC_HOTUNPLUG_EVENT	= 0x01,
};

/* XXX
 * Does a hotplug come from module insertion, or from detection
 * of each interface block (UniPro device) in a module?  Assume
 * the former for now.
 */
struct svc_function_hotplug {
	__u8	hotplug_event;	/* enum svc_function_hotplug_event */
	__u8	module_id;
	__u8	data[0];
};

enum svc_function_ddb_type {
	SVC_DDB_GET		= 0x00,
	SVC_DDB_RESPONSE	= 0x01,
};

/* XXX
 * Will only the first interface block in a module be responsible
 * for this?  If a module has two interface blocks, will both supply
 * the same information, or will it be partitioned?  For now assume
 * it's a per-module thing.
 */
struct svc_function_ddb_get {
	__u8	module_id;
	__u8	message_id;
};

struct svc_function_ddb_response {
	__u8	module_id;
	__u8	message_id;
	__le16	descriptor_length;
	__u8	ddb[0];
};

struct svc_function_ddb {
	__u8	ddb_type;	/* enum svc_function_ddb_type */
	union {
		struct svc_function_ddb_get		ddb_get;
		struct svc_function_ddb_response	ddb_response;
	};
};

enum svc_function_power_type {
	SVC_POWER_BATTERY_STATUS		= 0x00,
	SVC_POWER_BATTERY_STATUS_REQUEST	= 0x01,
};

enum svc_function_battery_status {
	SVC_BATTERY_UNKNOWN		= 0x00,
	SVC_BATTERY_CHARGING		= 0x01,
	SVC_BATTERY_DISCHARGING		= 0x02,
	SVC_BATTERY_NOT_CHARGING	= 0x03,
	SVC_BATTERY_FULL		= 0x04,
};

struct svc_function_power_battery_status {
	__le16	charge_full;
	__le16	charge_now;
	__u8	status;	/* enum svc_function_battery_status */
};

struct svc_function_power_battery_status_request {
};

/* XXX
 * Each interface block carries power, so it's possible these things
 * are associated with each UniPro device and not just the module.
 * For now it's safe to assume it's per-module.
 */
struct svc_function_power {
	__u8	power_type;	/* enum svc_function_power_type */
	__u8	module_id;
	union {
		struct svc_function_power_battery_status		status;
		struct svc_function_power_battery_status_request	request;
	};
};

enum svc_function_epm_command_type {
	SVC_EPM_ENABLE	= 0x00,
	SVC_EPM_DISABLE	= 0x01,
};

/* EPM's are associated with the module */
struct svc_function_epm {
	__u8	epm_command_type;	/* enum svc_function_epm_command_type */
	__u8	module_id;
};

enum svc_function_suspend_command_type {
	SVC_SUSPEND_FIXME_1	= 0x00,	// FIXME
	SVC_SUSPEND_FIXME_2	= 0x01,
};

/* We'll want independent control for multi-interface block modules */
struct svc_function_suspend {
	__u8	suspend_command_type;	/* enum function_suspend_command_type */
	__u8	device_id;
};

struct svc_msg {
	struct svc_msg_header	header;
	union {
		struct svc_function_handshake		handshake;
		struct svc_function_unipro_management	management;
		struct svc_function_hotplug		hotplug;
		struct svc_function_ddb			ddb;
		struct svc_function_power		power;
		struct svc_function_epm			epm;
		struct svc_function_suspend		suspend;
	};
};

#pragma pack(pop)

#endif /* __SVC_MSG_H */
