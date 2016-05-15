/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 - 2015 Google Inc. All rights reserved.
 * Copyright(c) 2014 - 2015 Linaro Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 - 2015 Google Inc. All rights reserved.
 * Copyright(c) 2014 - 2015 Linaro Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google Inc. or Linaro Ltd. nor the names of
 *    its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL GOOGLE INC. OR
 * LINARO LTD. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GREYBUS_PROTOCOLS_H
#define __GREYBUS_PROTOCOLS_H

/* Fixed IDs for control/svc protocols */

/* SVC switch-port device ids */
#define GB_SVC_DEVICE_ID_SVC			0
#define GB_SVC_DEVICE_ID_AP			1
#define GB_SVC_DEVICE_ID_MIN			2
#define GB_SVC_DEVICE_ID_MAX			31

#define GB_SVC_CPORT_ID				0
#define GB_CONTROL_BUNDLE_ID			0
#define GB_CONTROL_CPORT_ID			0


/*
 * All operation messages (both requests and responses) begin with
 * a header that encodes the size of the message (header included).
 * This header also contains a unique identifier, that associates a
 * response message with its operation.  The header contains an
 * operation type field, whose interpretation is dependent on what
 * type of protocol is used over the connection.  The high bit
 * (0x80) of the operation type field is used to indicate whether
 * the message is a request (clear) or a response (set).
 *
 * Response messages include an additional result byte, which
 * communicates the result of the corresponding request.  A zero
 * result value means the operation completed successfully.  Any
 * other value indicates an error; in this case, the payload of the
 * response message (if any) is ignored.  The result byte must be
 * zero in the header for a request message.
 *
 * The wire format for all numeric fields in the header is little
 * endian.  Any operation-specific data begins immediately after the
 * header.
 */
struct gb_operation_msg_hdr {
	__le16	size;		/* Size in bytes of header + payload */
	__le16	operation_id;	/* Operation unique id */
	__u8	type;		/* E.g GB_I2C_TYPE_* or GB_GPIO_TYPE_* */
	__u8	result;		/* Result of request (in responses only) */
	__u8	pad[2];		/* must be zero (ignore when read) */
} __packed;


/* Generic request types */
#define GB_REQUEST_TYPE_PROTOCOL_VERSION	0x01
#define GB_REQUEST_TYPE_INVALID			0x7f

struct gb_protocol_version_request {
	__u8	major;
	__u8	minor;
} __packed;

struct gb_protocol_version_response {
	__u8	major;
	__u8	minor;
} __packed;

/* Control Protocol */

/* Greybus control request types */
#define GB_CONTROL_TYPE_VERSION			0x01
#define GB_CONTROL_TYPE_PROBE_AP		0x02
#define GB_CONTROL_TYPE_GET_MANIFEST_SIZE	0x03
#define GB_CONTROL_TYPE_GET_MANIFEST		0x04
#define GB_CONTROL_TYPE_CONNECTED		0x05
#define GB_CONTROL_TYPE_DISCONNECTED		0x06
#define GB_CONTROL_TYPE_TIMESYNC_ENABLE		0x07
#define GB_CONTROL_TYPE_TIMESYNC_DISABLE	0x08
#define GB_CONTROL_TYPE_TIMESYNC_AUTHORITATIVE	0x09
/*	Unused					0x0a */
#define GB_CONTROL_TYPE_BUNDLE_VERSION		0x0b
#define GB_CONTROL_TYPE_TIMESYNC_GET_LAST_EVENT	0x0d
#define GB_CONTROL_TYPE_MODE_SWITCH		0x0e

struct gb_control_version_request {
	__u8	major;
	__u8	minor;
} __packed;

struct gb_control_version_response {
	__u8	major;
	__u8	minor;
} __packed;

struct gb_control_bundle_version_request {
	__u8	bundle_id;
} __packed;

struct gb_control_bundle_version_response {
	__u8	major;
	__u8	minor;
} __packed;

/* Control protocol manifest get size request has no payload*/
struct gb_control_get_manifest_size_response {
	__le16			size;
} __packed;

/* Control protocol manifest get request has no payload */
struct gb_control_get_manifest_response {
	__u8			data[0];
} __packed;

/* Control protocol [dis]connected request */
struct gb_control_connected_request {
	__le16			cport_id;
} __packed;

struct gb_control_disconnected_request {
	__le16			cport_id;
} __packed;
/* Control protocol [dis]connected response has no payload */

#define GB_TIMESYNC_MAX_STROBES			0x04

struct gb_control_timesync_enable_request {
	__u8	count;
	__le64	frame_time;
	__le32	strobe_delay;
	__le32	refclk;
} __packed;
/* timesync enable response has no payload */

struct gb_control_timesync_authoritative_request {
	__le64	frame_time[GB_TIMESYNC_MAX_STROBES];
} __packed;
/* timesync authoritative response has no payload */

/* timesync get_last_event_request has no payload */
struct gb_control_timesync_get_last_event_response {
	__le64	frame_time;
} __packed;

/* APBridge protocol */

/* request APB1 log */
#define GB_APB_REQUEST_LOG			0x02

/* request to map a cport to bulk in and bulk out endpoints */
#define GB_APB_REQUEST_EP_MAPPING		0x03

/* request to get the number of cports available */
#define GB_APB_REQUEST_CPORT_COUNT		0x04

/* request to reset a cport state */
#define GB_APB_REQUEST_RESET_CPORT		0x05

/* request to time the latency of messages on a given cport */
#define GB_APB_REQUEST_LATENCY_TAG_EN		0x06
#define GB_APB_REQUEST_LATENCY_TAG_DIS		0x07

/* request to control the CSI transmitter */
#define GB_APB_REQUEST_CSI_TX_CONTROL		0x08

/* request to control the CSI transmitter */
#define GB_APB_REQUEST_AUDIO_CONTROL		0x09

/* vendor requests to enable/disable CPort features */
#define GB_APB_REQUEST_CPORT_FEAT_EN		0x0b
#define GB_APB_REQUEST_CPORT_FEAT_DIS		0x0c

/* TimeSync commands */
#define REQUEST_TIMESYNC_ENABLE			0x0d
#define REQUEST_TIMESYNC_DISABLE		0x0e
#define REQUEST_TIMESYNC_AUTHORITATIVE		0x0f
#define REQUEST_TIMESYNC_GET_LAST_EVENT		0x10

/* Firmware Download Protocol */

/* Request Types */
#define GB_FW_DOWNLOAD_TYPE_FIND_FIRMWARE	0x01
#define GB_FW_DOWNLOAD_TYPE_FETCH_FIRMWARE	0x02
#define GB_FW_DOWNLOAD_TYPE_RELEASE_FIRMWARE	0x03

#define GB_FIRMWARE_TAG_MAX_LEN			10

/* firmware download find firmware request/response */
struct gb_fw_download_find_firmware_request {
	__u8			firmware_tag[GB_FIRMWARE_TAG_MAX_LEN];
} __packed;

struct gb_fw_download_find_firmware_response {
	__u8			firmware_id;
	__le32			size;
} __packed;

/* firmware download fetch firmware request/response */
struct gb_fw_download_fetch_firmware_request {
	__u8			firmware_id;
	__le32			offset;
	__le32			size;
} __packed;

struct gb_fw_download_fetch_firmware_response {
	__u8			data[0];
} __packed;

/* firmware download release firmware request */
struct gb_fw_download_release_firmware_request {
	__u8			firmware_id;
} __packed;
/* firmware download release firmware response has no payload */


/* Firmware Management Protocol */

/* Request Types */
#define GB_FW_MGMT_TYPE_INTERFACE_FW_VERSION	0x01
#define GB_FW_MGMT_TYPE_LOAD_AND_VALIDATE_FW	0x02
#define GB_FW_MGMT_TYPE_LOADED_FW		0x03
#define GB_FW_MGMT_TYPE_BACKEND_FW_VERSION	0x04
#define GB_FW_MGMT_TYPE_BACKEND_FW_UPDATE	0x05
#define GB_FW_MGMT_TYPE_BACKEND_FW_UPDATED	0x06

#define GB_FW_LOAD_METHOD_UNIPRO		0x01
#define GB_FW_LOAD_METHOD_INTERNAL		0x02

#define GB_FW_LOAD_STATUS_FAILED		0x00
#define GB_FW_LOAD_STATUS_UNVALIDATED		0x01
#define GB_FW_LOAD_STATUS_VALIDATED		0x02
#define GB_FW_LOAD_STATUS_VALIDATION_FAILED	0x03

#define GB_FW_BACKEND_FW_STATUS_SUCCESS		0x01
#define GB_FW_BACKEND_FW_STATUS_FAIL_FIND	0x02
#define GB_FW_BACKEND_FW_STATUS_FAIL_FETCH	0x03
#define GB_FW_BACKEND_FW_STATUS_FAIL_WRITE	0x04
#define GB_FW_BACKEND_FW_STATUS_INT		0x05

/* firmware management interface firmware version request has no payload */
struct gb_fw_mgmt_interface_fw_version_response {
	__u8			firmware_tag[GB_FIRMWARE_TAG_MAX_LEN];
	__le16			major;
	__le16			minor;
} __packed;

/* firmware management load and validate firmware request/response */
struct gb_fw_mgmt_load_and_validate_fw_request {
	__u8			request_id;
	__u8			load_method;
	__u8			firmware_tag[GB_FIRMWARE_TAG_MAX_LEN];
} __packed;
/* firmware management load and validate firmware response has no payload*/

/* firmware management loaded firmware request */
struct gb_fw_mgmt_loaded_fw_request {
	__u8			request_id;
	__u8			status;
	__le16			major;
	__le16			minor;
} __packed;
/* firmware management loaded firmware response has no payload */

/* firmware management backend firmware version request/response */
struct gb_fw_mgmt_backend_fw_version_request {
	__u8			firmware_tag[GB_FIRMWARE_TAG_MAX_LEN];
} __packed;

struct gb_fw_mgmt_backend_fw_version_response {
	__le16			major;
	__le16			minor;
} __packed;

/* firmware management backend firmware update request */
struct gb_fw_mgmt_backend_fw_update_request {
	__u8			request_id;
	__u8			firmware_tag[GB_FIRMWARE_TAG_MAX_LEN];
} __packed;
/* firmware management backend firmware update response has no payload */

/* firmware management backend firmware updated request */
struct gb_fw_mgmt_backend_fw_updated_request {
	__u8			request_id;
	__u8			status;
} __packed;
/* firmware management backend firmware updated response has no payload */


/* Bootrom Protocol */

/* Version of the Greybus bootrom protocol we support */
#define GB_BOOTROM_VERSION_MAJOR		0x00
#define GB_BOOTROM_VERSION_MINOR		0x01

/* Greybus bootrom request types */
#define GB_BOOTROM_TYPE_VERSION			0x01
#define GB_BOOTROM_TYPE_FIRMWARE_SIZE		0x02
#define GB_BOOTROM_TYPE_GET_FIRMWARE		0x03
#define GB_BOOTROM_TYPE_READY_TO_BOOT		0x04
#define GB_BOOTROM_TYPE_AP_READY		0x05	/* Request with no-payload */
#define GB_BOOTROM_TYPE_GET_VID_PID		0x06	/* Request with no-payload */

/* Greybus bootrom boot stages */
#define GB_BOOTROM_BOOT_STAGE_ONE		0x01 /* Reserved for the boot ROM */
#define GB_BOOTROM_BOOT_STAGE_TWO		0x02 /* Bootrom package to be loaded by the boot ROM */
#define GB_BOOTROM_BOOT_STAGE_THREE		0x03 /* Module personality package loaded by Stage 2 firmware */

/* Greybus bootrom ready to boot status */
#define GB_BOOTROM_BOOT_STATUS_INVALID		0x00 /* Firmware blob could not be validated */
#define GB_BOOTROM_BOOT_STATUS_INSECURE		0x01 /* Firmware blob is valid but insecure */
#define GB_BOOTROM_BOOT_STATUS_SECURE		0x02 /* Firmware blob is valid and secure */

/* Max bootrom data fetch size in bytes */
#define GB_BOOTROM_FETCH_MAX			2000

struct gb_bootrom_version_request {
	__u8	major;
	__u8	minor;
} __packed;

struct gb_bootrom_version_response {
	__u8	major;
	__u8	minor;
} __packed;

/* Bootrom protocol firmware size request/response */
struct gb_bootrom_firmware_size_request {
	__u8			stage;
} __packed;

struct gb_bootrom_firmware_size_response {
	__le32			size;
} __packed;

/* Bootrom protocol get firmware request/response */
struct gb_bootrom_get_firmware_request {
	__le32			offset;
	__le32			size;
} __packed;

struct gb_bootrom_get_firmware_response {
	__u8			data[0];
} __packed;

/* Bootrom protocol Ready to boot request */
struct gb_bootrom_ready_to_boot_request {
	__u8			status;
} __packed;
/* Bootrom protocol Ready to boot response has no payload */

/* Bootrom protocol get VID/PID request has no payload */
struct gb_bootrom_get_vid_pid_response {
	__le32			vendor_id;
	__le32			product_id;
} __packed;


/* Power Supply */

/* Version of the Greybus power supply protocol we support */
#define GB_POWER_SUPPLY_VERSION_MAJOR		0x00
#define GB_POWER_SUPPLY_VERSION_MINOR		0x01

/* Greybus power supply request types */
#define GB_POWER_SUPPLY_TYPE_GET_SUPPLIES		0x02
#define GB_POWER_SUPPLY_TYPE_GET_DESCRIPTION		0x03
#define GB_POWER_SUPPLY_TYPE_GET_PROP_DESCRIPTORS	0x04
#define GB_POWER_SUPPLY_TYPE_GET_PROPERTY		0x05
#define GB_POWER_SUPPLY_TYPE_SET_PROPERTY		0x06
#define GB_POWER_SUPPLY_TYPE_EVENT			0x07

/* Should match up with battery technologies in linux/power_supply.h */
#define GB_POWER_SUPPLY_TECH_UNKNOWN			0x0000
#define GB_POWER_SUPPLY_TECH_NiMH			0x0001
#define GB_POWER_SUPPLY_TECH_LION			0x0002
#define GB_POWER_SUPPLY_TECH_LIPO			0x0003
#define GB_POWER_SUPPLY_TECH_LiFe			0x0004
#define GB_POWER_SUPPLY_TECH_NiCd			0x0005
#define GB_POWER_SUPPLY_TECH_LiMn			0x0006

/* Should match up with power supply types in linux/power_supply.h */
#define GB_POWER_SUPPLY_UNKNOWN_TYPE			0x0000
#define GB_POWER_SUPPLY_BATTERY_TYPE			0x0001
#define GB_POWER_SUPPLY_UPS_TYPE			0x0002
#define GB_POWER_SUPPLY_MAINS_TYPE			0x0003
#define GB_POWER_SUPPLY_USB_TYPE			0x0004
#define GB_POWER_SUPPLY_USB_DCP_TYPE			0x0005
#define GB_POWER_SUPPLY_USB_CDP_TYPE			0x0006
#define GB_POWER_SUPPLY_USB_ACA_TYPE			0x0007

/* Should match up with power supply health in linux/power_supply.h */
#define GB_POWER_SUPPLY_HEALTH_UNKNOWN			0x0000
#define GB_POWER_SUPPLY_HEALTH_GOOD			0x0001
#define GB_POWER_SUPPLY_HEALTH_OVERHEAT			0x0002
#define GB_POWER_SUPPLY_HEALTH_DEAD			0x0003
#define GB_POWER_SUPPLY_HEALTH_OVERVOLTAGE		0x0004
#define GB_POWER_SUPPLY_HEALTH_UNSPEC_FAILURE		0x0005
#define GB_POWER_SUPPLY_HEALTH_COLD			0x0006
#define GB_POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE	0x0007
#define GB_POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE	0x0008

/* Should match up with battery status in linux/power_supply.h */
#define GB_POWER_SUPPLY_STATUS_UNKNOWN		0x0000
#define GB_POWER_SUPPLY_STATUS_CHARGING		0x0001
#define GB_POWER_SUPPLY_STATUS_DISCHARGING	0x0002
#define GB_POWER_SUPPLY_STATUS_NOT_CHARGING	0x0003
#define GB_POWER_SUPPLY_STATUS_FULL		0x0004

struct gb_power_supply_get_supplies_response {
	__u8	supplies_count;
} __packed;

struct gb_power_supply_get_description_request {
	__u8	psy_id;
} __packed;

struct gb_power_supply_get_description_response {
	__u8	manufacturer[32];
	__u8	model[32];
	__u8	serial_number[32];
	__le16	type;
	__u8	properties_count;
} __packed;

struct gb_power_supply_props_desc {
	__u8	property;
#define GB_POWER_SUPPLY_PROP_STATUS				0x00
#define GB_POWER_SUPPLY_PROP_CHARGE_TYPE			0x01
#define GB_POWER_SUPPLY_PROP_HEALTH				0x02
#define GB_POWER_SUPPLY_PROP_PRESENT				0x03
#define GB_POWER_SUPPLY_PROP_ONLINE				0x04
#define GB_POWER_SUPPLY_PROP_AUTHENTIC				0x05
#define GB_POWER_SUPPLY_PROP_TECHNOLOGY				0x06
#define GB_POWER_SUPPLY_PROP_CYCLE_COUNT			0x07
#define GB_POWER_SUPPLY_PROP_VOLTAGE_MAX			0x08
#define GB_POWER_SUPPLY_PROP_VOLTAGE_MIN			0x09
#define GB_POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN			0x0A
#define GB_POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN			0x0B
#define GB_POWER_SUPPLY_PROP_VOLTAGE_NOW			0x0C
#define GB_POWER_SUPPLY_PROP_VOLTAGE_AVG			0x0D
#define GB_POWER_SUPPLY_PROP_VOLTAGE_OCV			0x0E
#define GB_POWER_SUPPLY_PROP_VOLTAGE_BOOT			0x0F
#define GB_POWER_SUPPLY_PROP_CURRENT_MAX			0x10
#define GB_POWER_SUPPLY_PROP_CURRENT_NOW			0x11
#define GB_POWER_SUPPLY_PROP_CURRENT_AVG			0x12
#define GB_POWER_SUPPLY_PROP_CURRENT_BOOT			0x13
#define GB_POWER_SUPPLY_PROP_POWER_NOW				0x14
#define GB_POWER_SUPPLY_PROP_POWER_AVG				0x15
#define GB_POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN			0x16
#define GB_POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN		0x17
#define GB_POWER_SUPPLY_PROP_CHARGE_FULL			0x18
#define GB_POWER_SUPPLY_PROP_CHARGE_EMPTY			0x19
#define GB_POWER_SUPPLY_PROP_CHARGE_NOW				0x1A
#define GB_POWER_SUPPLY_PROP_CHARGE_AVG				0x1B
#define GB_POWER_SUPPLY_PROP_CHARGE_COUNTER			0x1C
#define GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT		0x1D
#define GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX	0x1E
#define GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE		0x1F
#define GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX	0x20
#define GB_POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT		0x21
#define GB_POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX		0x22
#define GB_POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT		0x23
#define GB_POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN			0x24
#define GB_POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN		0x25
#define GB_POWER_SUPPLY_PROP_ENERGY_FULL			0x26
#define GB_POWER_SUPPLY_PROP_ENERGY_EMPTY			0x27
#define GB_POWER_SUPPLY_PROP_ENERGY_NOW				0x28
#define GB_POWER_SUPPLY_PROP_ENERGY_AVG				0x29
#define GB_POWER_SUPPLY_PROP_CAPACITY				0x2A
#define GB_POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN			0x2B
#define GB_POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX			0x2C
#define GB_POWER_SUPPLY_PROP_CAPACITY_LEVEL			0x2D
#define GB_POWER_SUPPLY_PROP_TEMP				0x2E
#define GB_POWER_SUPPLY_PROP_TEMP_MAX				0x2F
#define GB_POWER_SUPPLY_PROP_TEMP_MIN				0x30
#define GB_POWER_SUPPLY_PROP_TEMP_ALERT_MIN			0x31
#define GB_POWER_SUPPLY_PROP_TEMP_ALERT_MAX			0x32
#define GB_POWER_SUPPLY_PROP_TEMP_AMBIENT			0x33
#define GB_POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MIN		0x34
#define GB_POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MAX		0x35
#define GB_POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW			0x36
#define GB_POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG			0x37
#define GB_POWER_SUPPLY_PROP_TIME_TO_FULL_NOW			0x38
#define GB_POWER_SUPPLY_PROP_TIME_TO_FULL_AVG			0x39
#define GB_POWER_SUPPLY_PROP_TYPE				0x3A
#define GB_POWER_SUPPLY_PROP_SCOPE				0x3B
#define GB_POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT		0x3C
#define GB_POWER_SUPPLY_PROP_CALIBRATE				0x3D
	__u8	is_writeable;
} __packed;

struct gb_power_supply_get_property_descriptors_request {
	__u8	psy_id;
} __packed;

struct gb_power_supply_get_property_descriptors_response {
	__u8	properties_count;
	struct gb_power_supply_props_desc props[];
} __packed;

struct gb_power_supply_get_property_request {
	__u8	psy_id;
	__u8	property;
} __packed;

struct gb_power_supply_get_property_response {
	__le32	prop_val;
};

struct gb_power_supply_set_property_request {
	__u8	psy_id;
	__u8	property;
	__le32	prop_val;
} __packed;

struct gb_power_supply_event_request {
	__u8	psy_id;
	__u8	event;
#define GB_POWER_SUPPLY_UPDATE		0x01
} __packed;


/* HID */

/* Version of the Greybus hid protocol we support */
#define GB_HID_VERSION_MAJOR		0x00
#define GB_HID_VERSION_MINOR		0x01

/* Greybus HID operation types */
#define GB_HID_TYPE_GET_DESC		0x02
#define GB_HID_TYPE_GET_REPORT_DESC	0x03
#define GB_HID_TYPE_PWR_ON		0x04
#define GB_HID_TYPE_PWR_OFF		0x05
#define GB_HID_TYPE_GET_REPORT		0x06
#define GB_HID_TYPE_SET_REPORT		0x07
#define GB_HID_TYPE_IRQ_EVENT		0x08

/* Report type */
#define GB_HID_INPUT_REPORT		0
#define GB_HID_OUTPUT_REPORT		1
#define GB_HID_FEATURE_REPORT		2

/* Different request/response structures */
/* HID get descriptor response */
struct gb_hid_desc_response {
	__u8				bLength;
	__le16				wReportDescLength;
	__le16				bcdHID;
	__le16				wProductID;
	__le16				wVendorID;
	__u8				bCountryCode;
} __packed;

/* HID get report request/response */
struct gb_hid_get_report_request {
	__u8				report_type;
	__u8				report_id;
} __packed;

/* HID set report request */
struct gb_hid_set_report_request {
	__u8				report_type;
	__u8				report_id;
	__u8				report[0];
} __packed;

/* HID input report request, via interrupt pipe */
struct gb_hid_input_report_request {
	__u8				report[0];
} __packed;


/* I2C */

/* Version of the Greybus i2c protocol we support */
#define GB_I2C_VERSION_MAJOR		0x00
#define GB_I2C_VERSION_MINOR		0x01

/* Greybus i2c request types */
#define GB_I2C_TYPE_FUNCTIONALITY	0x02
#define GB_I2C_TYPE_TRANSFER		0x05

/* functionality request has no payload */
struct gb_i2c_functionality_response {
	__le32	functionality;
} __packed;

/*
 * Outgoing data immediately follows the op count and ops array.
 * The data for each write (master -> slave) op in the array is sent
 * in order, with no (e.g. pad) bytes separating them.
 *
 * Short reads cause the entire transfer request to fail So response
 * payload consists only of bytes read, and the number of bytes is
 * exactly what was specified in the corresponding op.  Like
 * outgoing data, the incoming data is in order and contiguous.
 */
struct gb_i2c_transfer_op {
	__le16	addr;
	__le16	flags;
	__le16	size;
} __packed;

struct gb_i2c_transfer_request {
	__le16				op_count;
	struct gb_i2c_transfer_op	ops[0];		/* op_count of these */
} __packed;
struct gb_i2c_transfer_response {
	__u8				data[0];	/* inbound data */
} __packed;


/* GPIO */

/* Version of the Greybus GPIO protocol we support */
#define GB_GPIO_VERSION_MAJOR		0x00
#define GB_GPIO_VERSION_MINOR		0x01

/* Greybus GPIO request types */
#define GB_GPIO_TYPE_LINE_COUNT		0x02
#define GB_GPIO_TYPE_ACTIVATE		0x03
#define GB_GPIO_TYPE_DEACTIVATE		0x04
#define GB_GPIO_TYPE_GET_DIRECTION	0x05
#define GB_GPIO_TYPE_DIRECTION_IN	0x06
#define GB_GPIO_TYPE_DIRECTION_OUT	0x07
#define GB_GPIO_TYPE_GET_VALUE		0x08
#define GB_GPIO_TYPE_SET_VALUE		0x09
#define GB_GPIO_TYPE_SET_DEBOUNCE	0x0a
#define GB_GPIO_TYPE_IRQ_TYPE		0x0b
#define GB_GPIO_TYPE_IRQ_MASK		0x0c
#define GB_GPIO_TYPE_IRQ_UNMASK		0x0d
#define GB_GPIO_TYPE_IRQ_EVENT		0x0e

#define GB_GPIO_IRQ_TYPE_NONE		0x00
#define GB_GPIO_IRQ_TYPE_EDGE_RISING	0x01
#define GB_GPIO_IRQ_TYPE_EDGE_FALLING	0x02
#define GB_GPIO_IRQ_TYPE_EDGE_BOTH	0x03
#define GB_GPIO_IRQ_TYPE_LEVEL_HIGH	0x04
#define GB_GPIO_IRQ_TYPE_LEVEL_LOW	0x08

/* line count request has no payload */
struct gb_gpio_line_count_response {
	__u8	count;
} __packed;

struct gb_gpio_activate_request {
	__u8	which;
} __packed;
/* activate response has no payload */

struct gb_gpio_deactivate_request {
	__u8	which;
} __packed;
/* deactivate response has no payload */

struct gb_gpio_get_direction_request {
	__u8	which;
} __packed;
struct gb_gpio_get_direction_response {
	__u8	direction;
} __packed;

struct gb_gpio_direction_in_request {
	__u8	which;
} __packed;
/* direction in response has no payload */

struct gb_gpio_direction_out_request {
	__u8	which;
	__u8	value;
} __packed;
/* direction out response has no payload */

struct gb_gpio_get_value_request {
	__u8	which;
} __packed;
struct gb_gpio_get_value_response {
	__u8	value;
} __packed;

struct gb_gpio_set_value_request {
	__u8	which;
	__u8	value;
} __packed;
/* set value response has no payload */

struct gb_gpio_set_debounce_request {
	__u8	which;
	__le16	usec;
} __packed;
/* debounce response has no payload */

struct gb_gpio_irq_type_request {
	__u8	which;
	__u8	type;
} __packed;
/* irq type response has no payload */

struct gb_gpio_irq_mask_request {
	__u8	which;
} __packed;
/* irq mask response has no payload */

struct gb_gpio_irq_unmask_request {
	__u8	which;
} __packed;
/* irq unmask response has no payload */

/* irq event requests originate on another module and are handled on the AP */
struct gb_gpio_irq_event_request {
	__u8	which;
} __packed;
/* irq event has no response */


/* PWM */

/* Version of the Greybus PWM protocol we support */
#define GB_PWM_VERSION_MAJOR		0x00
#define GB_PWM_VERSION_MINOR		0x01

/* Greybus PWM operation types */
#define GB_PWM_TYPE_PWM_COUNT		0x02
#define GB_PWM_TYPE_ACTIVATE		0x03
#define GB_PWM_TYPE_DEACTIVATE		0x04
#define GB_PWM_TYPE_CONFIG		0x05
#define GB_PWM_TYPE_POLARITY		0x06
#define GB_PWM_TYPE_ENABLE		0x07
#define GB_PWM_TYPE_DISABLE		0x08

/* pwm count request has no payload */
struct gb_pwm_count_response {
	__u8	count;
} __packed;

struct gb_pwm_activate_request {
	__u8	which;
} __packed;

struct gb_pwm_deactivate_request {
	__u8	which;
} __packed;

struct gb_pwm_config_request {
	__u8	which;
	__le32	duty;
	__le32	period;
} __packed;

struct gb_pwm_polarity_request {
	__u8	which;
	__u8	polarity;
} __packed;

struct gb_pwm_enable_request {
	__u8	which;
} __packed;

struct gb_pwm_disable_request {
	__u8	which;
} __packed;

/* SPI */

/* Version of the Greybus spi protocol we support */
#define GB_SPI_VERSION_MAJOR		0x00
#define GB_SPI_VERSION_MINOR		0x01

/* Should match up with modes in linux/spi/spi.h */
#define GB_SPI_MODE_CPHA		0x01		/* clock phase */
#define GB_SPI_MODE_CPOL		0x02		/* clock polarity */
#define GB_SPI_MODE_MODE_0		(0|0)		/* (original MicroWire) */
#define GB_SPI_MODE_MODE_1		(0|GB_SPI_MODE_CPHA)
#define GB_SPI_MODE_MODE_2		(GB_SPI_MODE_CPOL|0)
#define GB_SPI_MODE_MODE_3		(GB_SPI_MODE_CPOL|GB_SPI_MODE_CPHA)
#define GB_SPI_MODE_CS_HIGH		0x04		/* chipselect active high? */
#define GB_SPI_MODE_LSB_FIRST		0x08		/* per-word bits-on-wire */
#define GB_SPI_MODE_3WIRE		0x10		/* SI/SO signals shared */
#define GB_SPI_MODE_LOOP		0x20		/* loopback mode */
#define GB_SPI_MODE_NO_CS		0x40		/* 1 dev/bus, no chipselect */
#define GB_SPI_MODE_READY		0x80		/* slave pulls low to pause */

/* Should match up with flags in linux/spi/spi.h */
#define GB_SPI_FLAG_HALF_DUPLEX		BIT(0)		/* can't do full duplex */
#define GB_SPI_FLAG_NO_RX		BIT(1)		/* can't do buffer read */
#define GB_SPI_FLAG_NO_TX		BIT(2)		/* can't do buffer write */

/* Greybus spi operation types */
#define GB_SPI_TYPE_MASTER_CONFIG	0x02
#define GB_SPI_TYPE_DEVICE_CONFIG	0x03
#define GB_SPI_TYPE_TRANSFER		0x04

/* mode request has no payload */
struct gb_spi_master_config_response {
	__le32	bits_per_word_mask;
	__le32	min_speed_hz;
	__le32	max_speed_hz;
	__le16	mode;
	__le16	flags;
	__u8	num_chipselect;
} __packed;

struct gb_spi_device_config_request {
	__u8	chip_select;
} __packed;

struct gb_spi_device_config_response {
	__le16	mode;
	__u8	bits_per_word;
	__le32	max_speed_hz;
	__u8	device_type;
#define GB_SPI_SPI_DEV		0x00
#define GB_SPI_SPI_NOR		0x01
#define GB_SPI_SPI_MODALIAS	0x02
	__u8	name[32];
} __packed;

/**
 * struct gb_spi_transfer - a read/write buffer pair
 * @speed_hz: Select a speed other than the device default for this transfer. If
 *	0 the default (from @spi_device) is used.
 * @len: size of rx and tx buffers (in bytes)
 * @delay_usecs: microseconds to delay after this transfer before (optionally)
 * 	changing the chipselect status, then starting the next transfer or
 * 	completing this spi_message.
 * @cs_change: affects chipselect after this transfer completes
 * @bits_per_word: select a bits_per_word other than the device default for this
 *	transfer. If 0 the default (from @spi_device) is used.
 */
struct gb_spi_transfer {
	__le32		speed_hz;
	__le32		len;
	__le16		delay_usecs;
	__u8		cs_change;
	__u8		bits_per_word;
	__u8		rdwr;
#define GB_SPI_XFER_READ	0x01
#define GB_SPI_XFER_WRITE	0x02
} __packed;

struct gb_spi_transfer_request {
	__u8			chip_select;	/* of the spi device */
	__u8			mode;		/* of the spi device */
	__le16			count;
	struct gb_spi_transfer	transfers[0];	/* count of these */
} __packed;

struct gb_spi_transfer_response {
	__u8			data[0];	/* inbound data */
} __packed;

/* Version of the Greybus SVC protocol we support */
#define GB_SVC_VERSION_MAJOR		0x00
#define GB_SVC_VERSION_MINOR		0x01

/* Greybus SVC request types */
#define GB_SVC_TYPE_PROTOCOL_VERSION		0x01
#define GB_SVC_TYPE_SVC_HELLO			0x02
#define GB_SVC_TYPE_INTF_DEVICE_ID		0x03
#define GB_SVC_TYPE_INTF_HOTPLUG		0x04
#define GB_SVC_TYPE_INTF_HOT_UNPLUG		0x05
#define GB_SVC_TYPE_INTF_RESET			0x06
#define GB_SVC_TYPE_CONN_CREATE			0x07
#define GB_SVC_TYPE_CONN_DESTROY		0x08
#define GB_SVC_TYPE_DME_PEER_GET		0x09
#define GB_SVC_TYPE_DME_PEER_SET		0x0a
#define GB_SVC_TYPE_ROUTE_CREATE		0x0b
#define GB_SVC_TYPE_ROUTE_DESTROY		0x0c
#define GB_SVC_TYPE_TIMESYNC_ENABLE		0x0d
#define GB_SVC_TYPE_TIMESYNC_DISABLE		0x0e
#define GB_SVC_TYPE_TIMESYNC_AUTHORITATIVE	0x0f
#define GB_SVC_TYPE_INTF_SET_PWRM		0x10
#define GB_SVC_TYPE_INTF_EJECT			0x11
#define GB_SVC_TYPE_KEY_EVENT			0x12
#define GB_SVC_TYPE_PING			0x13
#define GB_SVC_TYPE_PWRMON_RAIL_COUNT_GET	0x14
#define GB_SVC_TYPE_PWRMON_RAIL_NAMES_GET	0x15
#define GB_SVC_TYPE_PWRMON_SAMPLE_GET		0x16
#define GB_SVC_TYPE_PWRMON_INTF_SAMPLE_GET	0x17
#define GB_SVC_TYPE_TIMESYNC_WAKE_PINS_ACQUIRE	0x18
#define GB_SVC_TYPE_TIMESYNC_WAKE_PINS_RELEASE	0x19
#define GB_SVC_TYPE_TIMESYNC_PING		0x1a
#define GB_SVC_TYPE_MODULE_INSERTED		0x1f
#define GB_SVC_TYPE_MODULE_REMOVED		0x20
#define GB_SVC_TYPE_INTF_VSYS_ENABLE		0x21
#define GB_SVC_TYPE_INTF_VSYS_DISABLE		0x22
#define GB_SVC_TYPE_INTF_REFCLK_ENABLE		0x23
#define GB_SVC_TYPE_INTF_REFCLK_DISABLE		0x24
#define GB_SVC_TYPE_INTF_UNIPRO_ENABLE		0x25
#define GB_SVC_TYPE_INTF_UNIPRO_DISABLE		0x26
#define GB_SVC_TYPE_INTF_ACTIVATE		0x27
#define GB_SVC_TYPE_INTF_MAILBOX_EVENT		0x29

/* Greybus SVC protocol status values */
#define GB_SVC_OP_SUCCESS			0x00
#define GB_SVC_OP_UNKNOWN_ERROR			0x01
#define GB_SVC_INTF_NOT_DETECTED		0x02
#define GB_SVC_INTF_NO_UPRO_LINK		0x03
#define GB_SVC_INTF_UPRO_NOT_DOWN		0x04
#define GB_SVC_INTF_UPRO_NOT_HIBERNATED		0x05
#define GB_SVC_INTF_NO_V_SYS			0x06
#define GB_SVC_INTF_V_CHG			0x07
#define GB_SVC_INTF_WAKE_BUSY			0x08
#define GB_SVC_INTF_NO_REFCLK			0x09
#define GB_SVC_INTF_RELEASING			0x0a
#define GB_SVC_INTF_NO_ORDER			0x0b
#define GB_SVC_INTF_MBOX_SET			0x0c
#define GB_SVC_INTF_BAD_MBOX			0x0d
#define GB_SVC_INTF_OP_TIMEOUT			0x0e
#define GB_SVC_PWRMON_OP_NOT_PRESENT		0x0f

struct gb_svc_version_request {
	__u8	major;
	__u8	minor;
} __packed;

struct gb_svc_version_response {
	__u8	major;
	__u8	minor;
} __packed;

/* SVC protocol hello request */
struct gb_svc_hello_request {
	__le16			endo_id;
	__u8			interface_id;
} __packed;
/* hello response has no payload */

struct gb_svc_intf_device_id_request {
	__u8	intf_id;
	__u8	device_id;
} __packed;
/* device id response has no payload */

struct gb_svc_intf_hotplug_request {
	__u8	intf_id;
	struct {
		__le32	ddbl1_mfr_id;
		__le32	ddbl1_prod_id;
		__le32	ara_vend_id;
		__le32	ara_prod_id;
		__le64	serial_number;
	} data;
} __packed;
/* hotplug response has no payload */

struct gb_svc_intf_hot_unplug_request {
	__u8	intf_id;
} __packed;
/* hot unplug response has no payload */

struct gb_svc_intf_reset_request {
	__u8	intf_id;
} __packed;
/* interface reset response has no payload */

struct gb_svc_intf_eject_request {
	__u8	intf_id;
} __packed;
/* interface eject response has no payload */

struct gb_svc_conn_create_request {
	__u8	intf1_id;
	__le16	cport1_id;
	__u8	intf2_id;
	__le16	cport2_id;
	__u8	tc;
	__u8	flags;
} __packed;
/* connection create response has no payload */

struct gb_svc_conn_destroy_request {
	__u8	intf1_id;
	__le16	cport1_id;
	__u8	intf2_id;
	__le16	cport2_id;
} __packed;
/* connection destroy response has no payload */

struct gb_svc_dme_peer_get_request {
	__u8	intf_id;
	__le16	attr;
	__le16	selector;
} __packed;

struct gb_svc_dme_peer_get_response {
	__le16	result_code;
	__le32	attr_value;
} __packed;

struct gb_svc_dme_peer_set_request {
	__u8	intf_id;
	__le16	attr;
	__le16	selector;
	__le32	value;
} __packed;

struct gb_svc_dme_peer_set_response {
	__le16	result_code;
} __packed;

/* Greybus init-status values, currently retrieved using DME peer gets. */
#define GB_INIT_SPI_BOOT_STARTED			0x02
#define GB_INIT_TRUSTED_SPI_BOOT_FINISHED		0x03
#define GB_INIT_UNTRUSTED_SPI_BOOT_FINISHED		0x04
#define GB_INIT_BOOTROM_UNIPRO_BOOT_STARTED		0x06
#define GB_INIT_BOOTROM_FALLBACK_UNIPRO_BOOT_STARTED	0x09

struct gb_svc_route_create_request {
	__u8	intf1_id;
	__u8	dev1_id;
	__u8	intf2_id;
	__u8	dev2_id;
} __packed;
/* route create response has no payload */

struct gb_svc_route_destroy_request {
	__u8	intf1_id;
	__u8	intf2_id;
} __packed;
/* route destroy response has no payload */

/* used for svc_intf_vsys_{enable,disable} */
struct gb_svc_intf_vsys_request {
	__u8	intf_id;
} __packed;

struct gb_svc_intf_vsys_response {
	__u8	result_code;
#define GB_SVC_INTF_VSYS_OK				0x00
#define GB_SVC_INTF_VSYS_BUSY				0x01
#define GB_SVC_INTF_VSYS_FAIL				0x02
} __packed;

/* used for svc_intf_refclk_{enable,disable} */
struct gb_svc_intf_refclk_request {
	__u8	intf_id;
} __packed;

struct gb_svc_intf_refclk_response {
	__u8	result_code;
#define GB_SVC_INTF_REFCLK_OK				0x00
#define GB_SVC_INTF_REFCLK_BUSY				0x01
#define GB_SVC_INTF_REFCLK_FAIL				0x02
} __packed;

/* used for svc_intf_unipro_{enable,disable} */
struct gb_svc_intf_unipro_request {
	__u8	intf_id;
} __packed;

struct gb_svc_intf_unipro_response {
	__u8	result_code;
#define GB_SVC_INTF_UNIPRO_OK				0x00
#define GB_SVC_INTF_UNIPRO_BUSY				0x01
#define GB_SVC_INTF_UNIPRO_FAIL				0x02
#define GB_SVC_INTF_UNIPRO_NOT_OFF			0x03
} __packed;

struct gb_svc_timesync_enable_request {
	__u8	count;
	__le64	frame_time;
	__le32	strobe_delay;
	__le32	refclk;
} __packed;
/* timesync enable response has no payload */

/* timesync authoritative request has no payload */
struct gb_svc_timesync_authoritative_response {
	__le64	frame_time[GB_TIMESYNC_MAX_STROBES];
};

struct gb_svc_timesync_wake_pins_acquire_request {
	__le32	strobe_mask;
};

/* timesync wake pins acquire response has no payload */

/* timesync wake pins release request has no payload */
/* timesync wake pins release response has no payload */

/* timesync svc ping request has no payload */
struct gb_svc_timesync_ping_response {
	__le64	frame_time;
} __packed;

#define GB_SVC_UNIPRO_FAST_MODE			0x01
#define GB_SVC_UNIPRO_SLOW_MODE			0x02
#define GB_SVC_UNIPRO_FAST_AUTO_MODE		0x04
#define GB_SVC_UNIPRO_SLOW_AUTO_MODE		0x05
#define GB_SVC_UNIPRO_MODE_UNCHANGED		0x07
#define GB_SVC_UNIPRO_HIBERNATE_MODE		0x11
#define GB_SVC_UNIPRO_OFF_MODE			0x12

#define GB_SVC_PWRM_RXTERMINATION		0x01
#define GB_SVC_PWRM_TXTERMINATION		0x02
#define GB_SVC_PWRM_LINE_RESET			0x04
#define GB_SVC_PWRM_SCRAMBLING			0x20

#define GB_SVC_PWRM_QUIRK_HSSER			0x00000001

#define GB_SVC_UNIPRO_HS_SERIES_A		0x01
#define GB_SVC_UNIPRO_HS_SERIES_B		0x02

struct gb_svc_intf_set_pwrm_request {
	__u8	intf_id;
	__u8	hs_series;
	__u8	tx_mode;
	__u8	tx_gear;
	__u8	tx_nlanes;
	__u8	rx_mode;
	__u8	rx_gear;
	__u8	rx_nlanes;
	__u8	flags;
	__le32	quirks;
} __packed;

struct gb_svc_intf_set_pwrm_response {
	__le16	result_code;
} __packed;

struct gb_svc_key_event_request {
	__le16  key_code;
#define GB_KEYCODE_ARA         0x00

	__u8    key_event;
#define GB_SVC_KEY_RELEASED    0x00
#define GB_SVC_KEY_PRESSED     0x01
} __packed;

#define GB_SVC_PWRMON_MAX_RAIL_COUNT		254

struct gb_svc_pwrmon_rail_count_get_response {
	__u8	rail_count;
} __packed;

#define GB_SVC_PWRMON_RAIL_NAME_BUFSIZE		32

struct gb_svc_pwrmon_rail_names_get_response {
	__u8	name[0][GB_SVC_PWRMON_RAIL_NAME_BUFSIZE];
} __packed;

#define GB_SVC_PWRMON_TYPE_CURR			0x01
#define GB_SVC_PWRMON_TYPE_VOL			0x02
#define GB_SVC_PWRMON_TYPE_PWR			0x03

#define GB_SVC_PWRMON_GET_SAMPLE_OK		0x00
#define GB_SVC_PWRMON_GET_SAMPLE_INVAL		0x01
#define GB_SVC_PWRMON_GET_SAMPLE_NOSUPP		0x02
#define GB_SVC_PWRMON_GET_SAMPLE_HWERR		0x03

struct gb_svc_pwrmon_sample_get_request {
	__u8	rail_id;
	__u8	measurement_type;
} __packed;

struct gb_svc_pwrmon_sample_get_response {
	__u8	result;
	__le32	measurement;
} __packed;

struct gb_svc_pwrmon_intf_sample_get_request {
	__u8	intf_id;
	__u8	measurement_type;
} __packed;

struct gb_svc_pwrmon_intf_sample_get_response {
	__u8	result;
	__le32	measurement;
} __packed;

#define GB_SVC_MODULE_INSERTED_FLAG_NO_PRIMARY	0x0001

struct gb_svc_module_inserted_request {
	__u8	primary_intf_id;
	__u8	intf_count;
	__le16	flags;
} __packed;
/* module_inserted response has no payload */

struct gb_svc_module_removed_request {
	__u8	primary_intf_id;
} __packed;
/* module_removed response has no payload */

struct gb_svc_intf_activate_request {
	__u8	intf_id;
} __packed;

#define GB_SVC_INTF_TYPE_UNKNOWN		0x00
#define GB_SVC_INTF_TYPE_DUMMY			0x01
#define GB_SVC_INTF_TYPE_UNIPRO			0x02
#define GB_SVC_INTF_TYPE_GREYBUS		0x03

struct gb_svc_intf_activate_response {
	__u8	status;
	__u8	intf_type;
} __packed;

#define GB_SVC_INTF_MAILBOX_NONE		0x00
#define GB_SVC_INTF_MAILBOX_AP			0x01
#define GB_SVC_INTF_MAILBOX_GREYBUS		0x02

struct gb_svc_intf_mailbox_event_request {
	__u8	intf_id;
	__le16	result_code;
	__le32	mailbox;
} __packed;
/* intf_mailbox_event response has no payload */


/* RAW */

/* Version of the Greybus raw protocol we support */
#define	GB_RAW_VERSION_MAJOR			0x00
#define	GB_RAW_VERSION_MINOR			0x01

/* Greybus raw request types */
#define	GB_RAW_TYPE_SEND			0x02

struct gb_raw_send_request {
	__le32	len;
	__u8	data[0];
} __packed;


/* UART */

/* Version of the Greybus UART protocol we support */
#define GB_UART_VERSION_MAJOR		0x00
#define GB_UART_VERSION_MINOR		0x01

/* Greybus UART operation types */
#define GB_UART_TYPE_SEND_DATA			0x02
#define GB_UART_TYPE_RECEIVE_DATA		0x03	/* Unsolicited data */
#define GB_UART_TYPE_SET_LINE_CODING		0x04
#define GB_UART_TYPE_SET_CONTROL_LINE_STATE	0x05
#define GB_UART_TYPE_SEND_BREAK			0x06
#define GB_UART_TYPE_SERIAL_STATE		0x07	/* Unsolicited data */

/* Represents data from AP -> Module */
struct gb_uart_send_data_request {
	__le16	size;
	__u8	data[0];
} __packed;

/* recv-data-request flags */
#define GB_UART_RECV_FLAG_FRAMING		0x01	/* Framing error */
#define GB_UART_RECV_FLAG_PARITY		0x02	/* Parity error */
#define GB_UART_RECV_FLAG_OVERRUN		0x04	/* Overrun error */
#define GB_UART_RECV_FLAG_BREAK			0x08	/* Break */

/* Represents data from Module -> AP */
struct gb_uart_recv_data_request {
	__le16	size;
	__u8	flags;
	__u8	data[0];
} __packed;

struct gb_uart_set_line_coding_request {
	__le32	rate;
	__u8	format;
#define GB_SERIAL_1_STOP_BITS			0
#define GB_SERIAL_1_5_STOP_BITS			1
#define GB_SERIAL_2_STOP_BITS			2

	__u8	parity;
#define GB_SERIAL_NO_PARITY			0
#define GB_SERIAL_ODD_PARITY			1
#define GB_SERIAL_EVEN_PARITY			2
#define GB_SERIAL_MARK_PARITY			3
#define GB_SERIAL_SPACE_PARITY			4

	__u8	data_bits;
} __packed;

/* output control lines */
#define GB_UART_CTRL_DTR			0x01
#define GB_UART_CTRL_RTS			0x02

struct gb_uart_set_control_line_state_request {
	__u8	control;
} __packed;

struct gb_uart_set_break_request {
	__u8	state;
} __packed;

/* input control lines and line errors */
#define GB_UART_CTRL_DCD			0x01
#define GB_UART_CTRL_DSR			0x02
#define GB_UART_CTRL_RI				0x04

struct gb_uart_serial_state_request {
	__u8	control;
} __packed;

/* Loopback */

/* Version of the Greybus loopback protocol we support */
#define GB_LOOPBACK_VERSION_MAJOR		0x00
#define GB_LOOPBACK_VERSION_MINOR		0x01

/* Greybus loopback request types */
#define GB_LOOPBACK_TYPE_PING			0x02
#define GB_LOOPBACK_TYPE_TRANSFER		0x03
#define GB_LOOPBACK_TYPE_SINK			0x04

/*
 * Loopback request/response header format should be identical
 * to simplify bandwidth and data movement analysis.
 */
struct gb_loopback_transfer_request {
	__le32	len;
	__le32  reserved0;
	__le32  reserved1;
	__u8	data[0];
} __packed;

struct gb_loopback_transfer_response {
	__le32	len;
	__le32	reserved0;
	__le32	reserved1;
	__u8	data[0];
} __packed;

/* SDIO */
/* Version of the Greybus sdio protocol we support */
#define GB_SDIO_VERSION_MAJOR		0x00
#define GB_SDIO_VERSION_MINOR		0x01

/* Greybus SDIO operation types */
#define GB_SDIO_TYPE_GET_CAPABILITIES		0x02
#define GB_SDIO_TYPE_SET_IOS			0x03
#define GB_SDIO_TYPE_COMMAND			0x04
#define GB_SDIO_TYPE_TRANSFER			0x05
#define GB_SDIO_TYPE_EVENT			0x06

/* get caps response: request has no payload */
struct gb_sdio_get_caps_response {
	__le32	caps;
#define GB_SDIO_CAP_NONREMOVABLE	0x00000001
#define GB_SDIO_CAP_4_BIT_DATA		0x00000002
#define GB_SDIO_CAP_8_BIT_DATA		0x00000004
#define GB_SDIO_CAP_MMC_HS		0x00000008
#define GB_SDIO_CAP_SD_HS		0x00000010
#define GB_SDIO_CAP_ERASE		0x00000020
#define GB_SDIO_CAP_1_2V_DDR		0x00000040
#define GB_SDIO_CAP_1_8V_DDR		0x00000080
#define GB_SDIO_CAP_POWER_OFF_CARD	0x00000100
#define GB_SDIO_CAP_UHS_SDR12		0x00000200
#define GB_SDIO_CAP_UHS_SDR25		0x00000400
#define GB_SDIO_CAP_UHS_SDR50		0x00000800
#define GB_SDIO_CAP_UHS_SDR104		0x00001000
#define GB_SDIO_CAP_UHS_DDR50		0x00002000
#define GB_SDIO_CAP_DRIVER_TYPE_A	0x00004000
#define GB_SDIO_CAP_DRIVER_TYPE_C	0x00008000
#define GB_SDIO_CAP_DRIVER_TYPE_D	0x00010000
#define GB_SDIO_CAP_HS200_1_2V		0x00020000
#define GB_SDIO_CAP_HS200_1_8V		0x00040000
#define GB_SDIO_CAP_HS400_1_2V		0x00080000
#define GB_SDIO_CAP_HS400_1_8V		0x00100000

	/* see possible values below at vdd */
	__le32 ocr;
	__le16 max_blk_count;
	__le16 max_blk_size;
	__le32 f_min;
	__le32 f_max;
} __packed;

/* set ios request: response has no payload */
struct gb_sdio_set_ios_request {
	__le32	clock;
	__le32	vdd;
#define GB_SDIO_VDD_165_195	0x00000001
#define GB_SDIO_VDD_20_21	0x00000002
#define GB_SDIO_VDD_21_22	0x00000004
#define GB_SDIO_VDD_22_23	0x00000008
#define GB_SDIO_VDD_23_24	0x00000010
#define GB_SDIO_VDD_24_25	0x00000020
#define GB_SDIO_VDD_25_26	0x00000040
#define GB_SDIO_VDD_26_27	0x00000080
#define GB_SDIO_VDD_27_28	0x00000100
#define GB_SDIO_VDD_28_29	0x00000200
#define GB_SDIO_VDD_29_30	0x00000400
#define GB_SDIO_VDD_30_31	0x00000800
#define GB_SDIO_VDD_31_32	0x00001000
#define GB_SDIO_VDD_32_33	0x00002000
#define GB_SDIO_VDD_33_34	0x00004000
#define GB_SDIO_VDD_34_35	0x00008000
#define GB_SDIO_VDD_35_36	0x00010000

	__u8	bus_mode;
#define GB_SDIO_BUSMODE_OPENDRAIN	0x00
#define GB_SDIO_BUSMODE_PUSHPULL	0x01

	__u8	power_mode;
#define GB_SDIO_POWER_OFF	0x00
#define GB_SDIO_POWER_UP	0x01
#define GB_SDIO_POWER_ON	0x02
#define GB_SDIO_POWER_UNDEFINED	0x03

	__u8	bus_width;
#define GB_SDIO_BUS_WIDTH_1	0x00
#define GB_SDIO_BUS_WIDTH_4	0x02
#define GB_SDIO_BUS_WIDTH_8	0x03

	__u8	timing;
#define GB_SDIO_TIMING_LEGACY		0x00
#define GB_SDIO_TIMING_MMC_HS		0x01
#define GB_SDIO_TIMING_SD_HS		0x02
#define GB_SDIO_TIMING_UHS_SDR12	0x03
#define GB_SDIO_TIMING_UHS_SDR25	0x04
#define GB_SDIO_TIMING_UHS_SDR50	0x05
#define GB_SDIO_TIMING_UHS_SDR104	0x06
#define GB_SDIO_TIMING_UHS_DDR50	0x07
#define GB_SDIO_TIMING_MMC_DDR52	0x08
#define GB_SDIO_TIMING_MMC_HS200	0x09
#define GB_SDIO_TIMING_MMC_HS400	0x0A

	__u8	signal_voltage;
#define GB_SDIO_SIGNAL_VOLTAGE_330	0x00
#define GB_SDIO_SIGNAL_VOLTAGE_180	0x01
#define GB_SDIO_SIGNAL_VOLTAGE_120	0x02

	__u8	drv_type;
#define GB_SDIO_SET_DRIVER_TYPE_B	0x00
#define GB_SDIO_SET_DRIVER_TYPE_A	0x01
#define GB_SDIO_SET_DRIVER_TYPE_C	0x02
#define GB_SDIO_SET_DRIVER_TYPE_D	0x03
} __packed;

/* command request */
struct gb_sdio_command_request {
	__u8	cmd;
	__u8	cmd_flags;
#define GB_SDIO_RSP_NONE		0x00
#define GB_SDIO_RSP_PRESENT		0x01
#define GB_SDIO_RSP_136			0x02
#define GB_SDIO_RSP_CRC			0x04
#define GB_SDIO_RSP_BUSY		0x08
#define GB_SDIO_RSP_OPCODE		0x10

	__u8	cmd_type;
#define GB_SDIO_CMD_AC		0x00
#define GB_SDIO_CMD_ADTC	0x01
#define GB_SDIO_CMD_BC		0x02
#define GB_SDIO_CMD_BCR		0x03

	__le32	cmd_arg;
	__le16	data_blocks;
	__le16	data_blksz;
} __packed;

struct gb_sdio_command_response {
	__le32	resp[4];
} __packed;

/* transfer request */
struct gb_sdio_transfer_request {
	__u8	data_flags;
#define GB_SDIO_DATA_WRITE	0x01
#define GB_SDIO_DATA_READ	0x02
#define GB_SDIO_DATA_STREAM	0x04

	__le16	data_blocks;
	__le16	data_blksz;
	__u8	data[0];
} __packed;

struct gb_sdio_transfer_response {
	__le16	data_blocks;
	__le16	data_blksz;
	__u8	data[0];
} __packed;

/* event request: generated by module and is defined as unidirectional */
struct gb_sdio_event_request {
	__u8	event;
#define GB_SDIO_CARD_INSERTED	0x01
#define GB_SDIO_CARD_REMOVED	0x02
#define GB_SDIO_WP		0x04
} __packed;

/* Camera */

#define GB_CAMERA_VERSION_MAJOR			0x00
#define GB_CAMERA_VERSION_MINOR			0x01

/* Greybus Camera request types */
#define GB_CAMERA_TYPE_CAPABILITIES		0x02
#define GB_CAMERA_TYPE_CONFIGURE_STREAMS	0x03
#define GB_CAMERA_TYPE_CAPTURE			0x04
#define GB_CAMERA_TYPE_FLUSH			0x05
#define GB_CAMERA_TYPE_METADATA			0x06

#define GB_CAMERA_MAX_STREAMS			4
#define GB_CAMERA_MAX_SETTINGS_SIZE		8192

/* Greybus Camera Configure Streams request payload */
struct gb_camera_stream_config_request {
	__le16 width;
	__le16 height;
	__le16 format;
	__le16 padding;
} __packed;

struct gb_camera_configure_streams_request {
	__u8 num_streams;
	__u8 flags;
#define GB_CAMERA_CONFIGURE_STREAMS_TEST_ONLY	0x01
	__le16 padding;
	struct gb_camera_stream_config_request config[0];
} __packed;

/* Greybus Camera Configure Streams response payload */
struct gb_camera_stream_config_response {
	__le16 width;
	__le16 height;
	__le16 format;
	__u8 virtual_channel;
	__u8 data_type[2];
	__u8 padding[3];
	__le32 max_size;
} __packed;

struct gb_camera_configure_streams_response {
	__u8 num_streams;
	__u8 flags;
#define GB_CAMERA_CONFIGURE_STREAMS_ADJUSTED	0x01
	__u8 num_lanes;
	__u8 padding;
	__le32 bus_freq;
	__le32 lines_per_second;
	struct gb_camera_stream_config_response config[0];
} __packed;

/* Greybus Camera Capture request payload - response has no payload */
struct gb_camera_capture_request {
	__le32 request_id;
	__u8 streams;
	__u8 padding;
	__le16 num_frames;
	__u8 settings[0];
} __packed;

/* Greybus Camera Flush response payload - request has no payload */
struct gb_camera_flush_response {
	__le32 request_id;
} __packed;

/* Greybus Camera Metadata request payload - operation has no response */
struct gb_camera_metadata_request {
	__le32 request_id;
	__le16 frame_number;
	__u8 stream;
	__u8 padding;
	__u8 metadata[0];
} __packed;

/* Lights */

#define GB_LIGHTS_VERSION_MAJOR 0x00
#define GB_LIGHTS_VERSION_MINOR 0x01

/* Greybus Lights request types */
#define GB_LIGHTS_TYPE_GET_LIGHTS		0x02
#define GB_LIGHTS_TYPE_GET_LIGHT_CONFIG		0x03
#define GB_LIGHTS_TYPE_GET_CHANNEL_CONFIG	0x04
#define GB_LIGHTS_TYPE_GET_CHANNEL_FLASH_CONFIG	0x05
#define GB_LIGHTS_TYPE_SET_BRIGHTNESS		0x06
#define GB_LIGHTS_TYPE_SET_BLINK		0x07
#define GB_LIGHTS_TYPE_SET_COLOR		0x08
#define GB_LIGHTS_TYPE_SET_FADE			0x09
#define GB_LIGHTS_TYPE_EVENT			0x0A
#define GB_LIGHTS_TYPE_SET_FLASH_INTENSITY	0x0B
#define GB_LIGHTS_TYPE_SET_FLASH_STROBE		0x0C
#define GB_LIGHTS_TYPE_SET_FLASH_TIMEOUT	0x0D
#define GB_LIGHTS_TYPE_GET_FLASH_FAULT		0x0E

/* Greybus Light modes */

/*
 * if you add any specific mode below, update also the
 * GB_CHANNEL_MODE_DEFINED_RANGE value accordingly
 */
#define GB_CHANNEL_MODE_NONE		0x00000000
#define GB_CHANNEL_MODE_BATTERY		0x00000001
#define GB_CHANNEL_MODE_POWER		0x00000002
#define GB_CHANNEL_MODE_WIRELESS	0x00000004
#define GB_CHANNEL_MODE_BLUETOOTH	0x00000008
#define GB_CHANNEL_MODE_KEYBOARD	0x00000010
#define GB_CHANNEL_MODE_BUTTONS		0x00000020
#define GB_CHANNEL_MODE_NOTIFICATION	0x00000040
#define GB_CHANNEL_MODE_ATTENTION	0x00000080
#define GB_CHANNEL_MODE_FLASH		0x00000100
#define GB_CHANNEL_MODE_TORCH		0x00000200
#define GB_CHANNEL_MODE_INDICATOR	0x00000400

/* Lights Mode valid bit values */
#define GB_CHANNEL_MODE_DEFINED_RANGE	0x000004FF
#define GB_CHANNEL_MODE_VENDOR_RANGE	0x00F00000

/* Greybus Light Channels Flags */
#define GB_LIGHT_CHANNEL_MULTICOLOR	0x00000001
#define GB_LIGHT_CHANNEL_FADER		0x00000002
#define GB_LIGHT_CHANNEL_BLINK		0x00000004

/* get count of lights in module */
struct gb_lights_get_lights_response {
	__u8	lights_count;
} __packed;

/* light config request payload */
struct gb_lights_get_light_config_request {
	__u8	id;
} __packed;

/* light config response payload */
struct gb_lights_get_light_config_response {
	__u8	channel_count;
	__u8	name[32];
} __packed;

/* channel config request payload */
struct gb_lights_get_channel_config_request {
	__u8	light_id;
	__u8	channel_id;
} __packed;

/* channel flash config request payload */
struct gb_lights_get_channel_flash_config_request {
	__u8	light_id;
	__u8	channel_id;
} __packed;

/* channel config response payload */
struct gb_lights_get_channel_config_response {
	__u8	max_brightness;
	__le32	flags;
	__le32	color;
	__u8	color_name[32];
	__le32	mode;
	__u8	mode_name[32];
} __packed;

/* channel flash config response payload */
struct gb_lights_get_channel_flash_config_response {
	__le32	intensity_min_uA;
	__le32	intensity_max_uA;
	__le32	intensity_step_uA;
	__le32	timeout_min_us;
	__le32	timeout_max_us;
	__le32	timeout_step_us;
} __packed;

/* blink request payload: response have no payload */
struct gb_lights_blink_request {
	__u8	light_id;
	__u8	channel_id;
	__le16	time_on_ms;
	__le16	time_off_ms;
} __packed;

/* set brightness request payload: response have no payload */
struct gb_lights_set_brightness_request {
	__u8	light_id;
	__u8	channel_id;
	__u8	brightness;
} __packed;

/* set color request payload: response have no payload */
struct gb_lights_set_color_request {
	__u8	light_id;
	__u8	channel_id;
	__le32	color;
} __packed;

/* set fade request payload: response have no payload */
struct gb_lights_set_fade_request {
	__u8	light_id;
	__u8	channel_id;
	__u8	fade_in;
	__u8	fade_out;
} __packed;

/* event request: generated by module */
struct gb_lights_event_request {
	__u8	light_id;
	__u8	event;
#define GB_LIGHTS_LIGHT_CONFIG		0x01
} __packed;

/* set flash intensity request payload: response have no payload */
struct gb_lights_set_flash_intensity_request {
	__u8	light_id;
	__u8	channel_id;
	__le32	intensity_uA;
} __packed;

/* set flash strobe state request payload: response have no payload */
struct gb_lights_set_flash_strobe_request {
	__u8	light_id;
	__u8	channel_id;
	__u8	state;
} __packed;

/* set flash timeout request payload: response have no payload */
struct gb_lights_set_flash_timeout_request {
	__u8	light_id;
	__u8	channel_id;
	__le32	timeout_us;
} __packed;

/* get flash fault request payload */
struct gb_lights_get_flash_fault_request {
	__u8	light_id;
	__u8	channel_id;
} __packed;

/* get flash fault response payload */
struct gb_lights_get_flash_fault_response {
	__le32	fault;
#define GB_LIGHTS_FLASH_FAULT_OVER_VOLTAGE		0x00000000
#define GB_LIGHTS_FLASH_FAULT_TIMEOUT			0x00000001
#define GB_LIGHTS_FLASH_FAULT_OVER_TEMPERATURE		0x00000002
#define GB_LIGHTS_FLASH_FAULT_SHORT_CIRCUIT		0x00000004
#define GB_LIGHTS_FLASH_FAULT_OVER_CURRENT		0x00000008
#define GB_LIGHTS_FLASH_FAULT_INDICATOR			0x00000010
#define GB_LIGHTS_FLASH_FAULT_UNDER_VOLTAGE		0x00000020
#define GB_LIGHTS_FLASH_FAULT_INPUT_VOLTAGE		0x00000040
#define GB_LIGHTS_FLASH_FAULT_LED_OVER_TEMPERATURE	0x00000080
} __packed;

/* Audio */

/* Version of the Greybus audio protocol we support */
#define GB_AUDIO_VERSION_MAJOR			0x00
#define GB_AUDIO_VERSION_MINOR			0x01

#define GB_AUDIO_TYPE_PROTOCOL_VERSION		0x01
#define GB_AUDIO_TYPE_GET_TOPOLOGY_SIZE		0x02
#define GB_AUDIO_TYPE_GET_TOPOLOGY		0x03
#define GB_AUDIO_TYPE_GET_CONTROL		0x04
#define GB_AUDIO_TYPE_SET_CONTROL		0x05
#define GB_AUDIO_TYPE_ENABLE_WIDGET		0x06
#define GB_AUDIO_TYPE_DISABLE_WIDGET		0x07
#define GB_AUDIO_TYPE_GET_PCM			0x08
#define GB_AUDIO_TYPE_SET_PCM			0x09
#define GB_AUDIO_TYPE_SET_TX_DATA_SIZE		0x0a
#define GB_AUDIO_TYPE_GET_TX_DELAY		0x0b
#define GB_AUDIO_TYPE_ACTIVATE_TX		0x0c
#define GB_AUDIO_TYPE_DEACTIVATE_TX		0x0d
#define GB_AUDIO_TYPE_SET_RX_DATA_SIZE		0x0e
#define GB_AUDIO_TYPE_GET_RX_DELAY		0x0f
#define GB_AUDIO_TYPE_ACTIVATE_RX		0x10
#define GB_AUDIO_TYPE_DEACTIVATE_RX		0x11
#define GB_AUDIO_TYPE_JACK_EVENT		0x12
#define GB_AUDIO_TYPE_BUTTON_EVENT		0x13
#define GB_AUDIO_TYPE_STREAMING_EVENT		0x14
#define GB_AUDIO_TYPE_SEND_DATA			0x15

/* Module must be able to buffer 10ms of audio data, minimum */
#define GB_AUDIO_SAMPLE_BUFFER_MIN_US		10000

#define GB_AUDIO_PCM_NAME_MAX			32
#define AUDIO_DAI_NAME_MAX			32
#define AUDIO_CONTROL_NAME_MAX			32
#define AUDIO_CTL_ELEM_NAME_MAX			44
#define AUDIO_ENUM_NAME_MAX			64
#define AUDIO_WIDGET_NAME_MAX			32

/* See SNDRV_PCM_FMTBIT_* in Linux source */
#define GB_AUDIO_PCM_FMT_S8			BIT(0)
#define GB_AUDIO_PCM_FMT_U8			BIT(1)
#define GB_AUDIO_PCM_FMT_S16_LE			BIT(2)
#define GB_AUDIO_PCM_FMT_S16_BE			BIT(3)
#define GB_AUDIO_PCM_FMT_U16_LE			BIT(4)
#define GB_AUDIO_PCM_FMT_U16_BE			BIT(5)
#define GB_AUDIO_PCM_FMT_S24_LE			BIT(6)
#define GB_AUDIO_PCM_FMT_S24_BE			BIT(7)
#define GB_AUDIO_PCM_FMT_U24_LE			BIT(8)
#define GB_AUDIO_PCM_FMT_U24_BE			BIT(9)
#define GB_AUDIO_PCM_FMT_S32_LE			BIT(10)
#define GB_AUDIO_PCM_FMT_S32_BE			BIT(11)
#define GB_AUDIO_PCM_FMT_U32_LE			BIT(12)
#define GB_AUDIO_PCM_FMT_U32_BE			BIT(13)

/* See SNDRV_PCM_RATE_* in Linux source */
#define GB_AUDIO_PCM_RATE_5512			BIT(0)
#define GB_AUDIO_PCM_RATE_8000			BIT(1)
#define GB_AUDIO_PCM_RATE_11025			BIT(2)
#define GB_AUDIO_PCM_RATE_16000			BIT(3)
#define GB_AUDIO_PCM_RATE_22050			BIT(4)
#define GB_AUDIO_PCM_RATE_32000			BIT(5)
#define GB_AUDIO_PCM_RATE_44100			BIT(6)
#define GB_AUDIO_PCM_RATE_48000			BIT(7)
#define GB_AUDIO_PCM_RATE_64000			BIT(8)
#define GB_AUDIO_PCM_RATE_88200			BIT(9)
#define GB_AUDIO_PCM_RATE_96000			BIT(10)
#define GB_AUDIO_PCM_RATE_176400		BIT(11)
#define GB_AUDIO_PCM_RATE_192000		BIT(12)

#define GB_AUDIO_STREAM_TYPE_CAPTURE		0x1
#define GB_AUDIO_STREAM_TYPE_PLAYBACK		0x2

#define GB_AUDIO_CTL_ELEM_ACCESS_READ		BIT(0)
#define GB_AUDIO_CTL_ELEM_ACCESS_WRITE		BIT(1)

/* See SNDRV_CTL_ELEM_TYPE_* in Linux source */
#define GB_AUDIO_CTL_ELEM_TYPE_BOOLEAN		0x01
#define GB_AUDIO_CTL_ELEM_TYPE_INTEGER		0x02
#define GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED	0x03
#define GB_AUDIO_CTL_ELEM_TYPE_INTEGER64	0x06

/* See SNDRV_CTL_ELEM_IFACE_* in Linux source */
#define GB_AUDIO_CTL_ELEM_IFACE_CARD		0x00
#define GB_AUDIO_CTL_ELEM_IFACE_HWDEP		0x01
#define GB_AUDIO_CTL_ELEM_IFACE_MIXER		0x02
#define GB_AUDIO_CTL_ELEM_IFACE_PCM		0x03
#define GB_AUDIO_CTL_ELEM_IFACE_RAWMIDI		0x04
#define GB_AUDIO_CTL_ELEM_IFACE_TIMER		0x05
#define GB_AUDIO_CTL_ELEM_IFACE_SEQUENCER	0x06

/* SNDRV_CTL_ELEM_ACCESS_* in Linux source */
#define GB_AUDIO_ACCESS_READ			BIT(0)
#define GB_AUDIO_ACCESS_WRITE			BIT(1)
#define GB_AUDIO_ACCESS_VOLATILE		BIT(2)
#define GB_AUDIO_ACCESS_TIMESTAMP		BIT(3)
#define GB_AUDIO_ACCESS_TLV_READ		BIT(4)
#define GB_AUDIO_ACCESS_TLV_WRITE		BIT(5)
#define GB_AUDIO_ACCESS_TLV_COMMAND		BIT(6)
#define GB_AUDIO_ACCESS_INACTIVE		BIT(7)
#define GB_AUDIO_ACCESS_LOCK			BIT(8)
#define GB_AUDIO_ACCESS_OWNER			BIT(9)

/* enum snd_soc_dapm_type */
#define GB_AUDIO_WIDGET_TYPE_INPUT		0x0
#define GB_AUDIO_WIDGET_TYPE_OUTPUT		0x1
#define GB_AUDIO_WIDGET_TYPE_MUX		0x2
#define GB_AUDIO_WIDGET_TYPE_VIRT_MUX		0x3
#define GB_AUDIO_WIDGET_TYPE_VALUE_MUX		0x4
#define GB_AUDIO_WIDGET_TYPE_MIXER		0x5
#define GB_AUDIO_WIDGET_TYPE_MIXER_NAMED_CTL	0x6
#define GB_AUDIO_WIDGET_TYPE_PGA		0x7
#define GB_AUDIO_WIDGET_TYPE_OUT_DRV		0x8
#define GB_AUDIO_WIDGET_TYPE_ADC		0x9
#define GB_AUDIO_WIDGET_TYPE_DAC		0xa
#define GB_AUDIO_WIDGET_TYPE_MICBIAS		0xb
#define GB_AUDIO_WIDGET_TYPE_MIC		0xc
#define GB_AUDIO_WIDGET_TYPE_HP			0xd
#define GB_AUDIO_WIDGET_TYPE_SPK		0xe
#define GB_AUDIO_WIDGET_TYPE_LINE		0xf
#define GB_AUDIO_WIDGET_TYPE_SWITCH		0x10
#define GB_AUDIO_WIDGET_TYPE_VMID		0x11
#define GB_AUDIO_WIDGET_TYPE_PRE		0x12
#define GB_AUDIO_WIDGET_TYPE_POST		0x13
#define GB_AUDIO_WIDGET_TYPE_SUPPLY		0x14
#define GB_AUDIO_WIDGET_TYPE_REGULATOR_SUPPLY	0x15
#define GB_AUDIO_WIDGET_TYPE_CLOCK_SUPPLY	0x16
#define GB_AUDIO_WIDGET_TYPE_AIF_IN		0x17
#define GB_AUDIO_WIDGET_TYPE_AIF_OUT		0x18
#define GB_AUDIO_WIDGET_TYPE_SIGGEN		0x19
#define GB_AUDIO_WIDGET_TYPE_DAI_IN		0x1a
#define GB_AUDIO_WIDGET_TYPE_DAI_OUT		0x1b
#define GB_AUDIO_WIDGET_TYPE_DAI_LINK		0x1c

#define GB_AUDIO_WIDGET_STATE_DISABLED		0x01
#define GB_AUDIO_WIDGET_STATE_ENAABLED		0x02

#define GB_AUDIO_JACK_EVENT_INSERTION		0x1
#define GB_AUDIO_JACK_EVENT_REMOVAL		0x2

#define GB_AUDIO_BUTTON_EVENT_PRESS		0x1
#define GB_AUDIO_BUTTON_EVENT_RELEASE		0x2

#define GB_AUDIO_STREAMING_EVENT_UNSPECIFIED	0x1
#define GB_AUDIO_STREAMING_EVENT_HALT		0x2
#define GB_AUDIO_STREAMING_EVENT_INTERNAL_ERROR	0x3
#define GB_AUDIO_STREAMING_EVENT_PROTOCOL_ERROR	0x4
#define GB_AUDIO_STREAMING_EVENT_FAILURE	0x5
#define GB_AUDIO_STREAMING_EVENT_UNDERRUN	0x6
#define GB_AUDIO_STREAMING_EVENT_OVERRUN	0x7
#define GB_AUDIO_STREAMING_EVENT_CLOCKING	0x8
#define GB_AUDIO_STREAMING_EVENT_DATA_LEN	0x9

#define GB_AUDIO_INVALID_INDEX			0xff

struct gb_audio_pcm {
	__u8	stream_name[GB_AUDIO_PCM_NAME_MAX];
	__le32	formats;	/* GB_AUDIO_PCM_FMT_* */
	__le32	rates;		/* GB_AUDIO_PCM_RATE_* */
	__u8	chan_min;
	__u8	chan_max;
	__u8	sig_bits;	/* number of bits of content */
} __packed;

struct gb_audio_dai {
	__u8			name[AUDIO_DAI_NAME_MAX];
	__le16			data_cport;
	struct gb_audio_pcm	capture;
	struct gb_audio_pcm	playback;
} __packed;

struct gb_audio_integer {
	__le32	min;
	__le32	max;
	__le32	step;
} __packed;

struct gb_audio_integer64 {
	__le64	min;
	__le64	max;
	__le64	step;
} __packed;

struct gb_audio_enumerated {
	__le32	items;
	__le16	names_length;
	__u8	names[0];
} __packed;

struct gb_audio_ctl_elem_info { /* See snd_ctl_elem_info in Linux source */
	__u8		type;		/* GB_AUDIO_CTL_ELEM_TYPE_* */
	__le16		dimen[4];
	union {
		struct gb_audio_integer		integer;
		struct gb_audio_integer64	integer64;
		struct gb_audio_enumerated	enumerated;
	} value;
} __packed;

struct gb_audio_ctl_elem_value { /* See snd_ctl_elem_value in Linux source */
	__le64				timestamp; /* XXX needed? */
	union {
		__le32	integer_value[2];	/* consider CTL_DOUBLE_xxx */
		__le64	integer64_value[2];
		__le32	enumerated_item[2];
	} value;
} __packed;

struct gb_audio_control {
	__u8	name[AUDIO_CONTROL_NAME_MAX];
	__u8	id;		/* 0-63 */
	__u8	iface;		/* GB_AUDIO_IFACE_* */
	__le16	data_cport;
	__le32	access;		/* GB_AUDIO_ACCESS_* */
	__u8    count;		/* count of same elements */
	__u8	count_values;	/* count of values, max=2 for CTL_DOUBLE_xxx */
	struct gb_audio_ctl_elem_info	info;
} __packed;

struct gb_audio_widget {
	__u8	name[AUDIO_WIDGET_NAME_MAX];
	__u8	sname[AUDIO_WIDGET_NAME_MAX];
	__u8	id;
	__u8	type;		/* GB_AUDIO_WIDGET_TYPE_* */
	__u8	state;		/* GB_AUDIO_WIDGET_STATE_* */
	__u8	ncontrols;
	struct gb_audio_control	ctl[0];	/* 'ncontrols' entries */
} __packed;

struct gb_audio_route {
	__u8	source_id;	/* widget id */
	__u8	destination_id;	/* widget id */
	__u8	control_id;	/* 0-63 */
	__u8	index;		/* Selection within the control */
} __packed;

struct gb_audio_topology {
	__u8	num_dais;
	__u8	num_controls;
	__u8	num_widgets;
	__u8	num_routes;
	__u32	size_dais;
	__u32	size_controls;
	__u32	size_widgets;
	__u32	size_routes;
	/*
	 * struct gb_audio_dai		dai[num_dais];
	 * struct gb_audio_control	controls[num_controls];
	 * struct gb_audio_widget	widgets[num_widgets];
	 * struct gb_audio_route	routes[num_routes];
	 */
	__u8	data[0];
} __packed;

struct gb_audio_get_topology_size_response {
	__le16	size;
} __packed;

struct gb_audio_get_topology_response {
	struct gb_audio_topology	topology;
} __packed;

struct gb_audio_get_control_request {
	__u8	control_id;
	__u8	index;
} __packed;

struct gb_audio_get_control_response {
	struct gb_audio_ctl_elem_value	value;
} __packed;

struct gb_audio_set_control_request {
	__u8	control_id;
	__u8	index;
	struct gb_audio_ctl_elem_value	value;
} __packed;

struct gb_audio_enable_widget_request {
	__u8	widget_id;
} __packed;

struct gb_audio_disable_widget_request {
	__u8	widget_id;
} __packed;

struct gb_audio_get_pcm_request {
	__le16	data_cport;
} __packed;

struct gb_audio_get_pcm_response {
	__le32	format;
	__le32	rate;
	__u8	channels;
	__u8	sig_bits;
} __packed;

struct gb_audio_set_pcm_request {
	__le16	data_cport;
	__le32	format;
	__le32	rate;
	__u8	channels;
	__u8	sig_bits;
} __packed;

struct gb_audio_set_tx_data_size_request {
	__le16	data_cport;
	__le16	size;
} __packed;

struct gb_audio_get_tx_delay_request {
	__le16	data_cport;
} __packed;

struct gb_audio_get_tx_delay_response {
	__le32	delay;
} __packed;

struct gb_audio_activate_tx_request {
	__le16	data_cport;
} __packed;

struct gb_audio_deactivate_tx_request {
	__le16	data_cport;
} __packed;

struct gb_audio_set_rx_data_size_request {
	__le16	data_cport;
	__le16	size;
} __packed;

struct gb_audio_get_rx_delay_request {
	__le16	data_cport;
} __packed;

struct gb_audio_get_rx_delay_response {
	__le32	delay;
} __packed;

struct gb_audio_activate_rx_request {
	__le16	data_cport;
} __packed;

struct gb_audio_deactivate_rx_request {
	__le16	data_cport;
} __packed;

struct gb_audio_jack_event_request {
	__u8	widget_id;
	__u8	jack_attribute;
	__u8	event;
} __packed;

struct gb_audio_button_event_request {
	__u8	widget_id;
	__u8	button_id;
	__u8	event;
} __packed;

struct gb_audio_streaming_event_request {
	__le16	data_cport;
	__u8	event;
} __packed;

struct gb_audio_send_data_request {
	__le64	timestamp;
	__u8	data[0];
} __packed;

#endif /* __GREYBUS_PROTOCOLS_H */

