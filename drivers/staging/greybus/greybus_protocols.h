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

/* Device ID of SVC and AP */
#define GB_DEVICE_ID_SVC			0
#define GB_DEVICE_ID_AP				1
#define GB_DEVICE_ID_MODULES_START		2

/*
 * Bundle/cport for control/svc cport: The same bundle/cport is shared by both
 * CONTROL and SVC protocols for communication between AP and SVC.
 */
#define GB_SVC_BUNDLE_ID			0
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
};


/* Generic request numbers supported by all modules */
#define GB_REQUEST_TYPE_INVALID			0x00
#define GB_REQUEST_TYPE_PROTOCOL_VERSION	0x01

/* Control Protocol */

/* version request has no payload */
struct gb_protocol_version_response {
	__u8	major;
	__u8	minor;
};

/* Control Protocol */

/* Version of the Greybus control protocol we support */
#define GB_CONTROL_VERSION_MAJOR		0x00
#define GB_CONTROL_VERSION_MINOR		0x01

/* Greybus control request types */
#define GB_CONTROL_TYPE_PROBE_AP		0x02
#define GB_CONTROL_TYPE_GET_MANIFEST_SIZE	0x03
#define GB_CONTROL_TYPE_GET_MANIFEST		0x04
#define GB_CONTROL_TYPE_CONNECTED		0x05
#define GB_CONTROL_TYPE_DISCONNECTED		0x06

/* Control protocol manifest get size request has no payload*/
struct gb_control_get_manifest_size_response {
	__le16			size;
};

/* Control protocol manifest get request has no payload */
struct gb_control_get_manifest_response {
	__u8			data[0];
};

/* Control protocol [dis]connected request */
struct gb_control_connected_request {
	__le16			cport_id;
};

struct gb_control_disconnected_request {
	__le16			cport_id;
};
/* Control protocol [dis]connected response has no payload */


/* Firmware Protocol */

/* Version of the Greybus firmware protocol we support */
#define GB_FIRMWARE_VERSION_MAJOR		0x00
#define GB_FIRMWARE_VERSION_MINOR		0x01

/* Greybus firmware request types */
#define GB_FIRMWARE_TYPE_FIRMWARE_SIZE		0x02
#define GB_FIRMWARE_TYPE_GET_FIRMWARE		0x03
#define GB_FIRMWARE_TYPE_READY_TO_BOOT		0x04

/* Greybus firmware boot stages */
#define GB_FIRMWARE_BOOT_STAGE_ONE		0x01 /* Reserved for the boot ROM */
#define GB_FIRMWARE_BOOT_STAGE_TWO		0x02 /* Firmware package to be loaded by the boot ROM */
#define GB_FIRMWARE_BOOT_STAGE_THREE		0x03 /* Module personality package loaded by Stage 2 firmware */

/* Greybus firmware ready to boot status */
#define GB_FIRMWARE_BOOT_STATUS_INVALID		0x00 /* Firmware blob could not be validated */
#define GB_FIRMWARE_BOOT_STATUS_INSECURE	0x01 /* Firmware blob is valid but insecure */
#define GB_FIRMWARE_BOOT_STATUS_SECURE		0x02 /* Firmware blob is valid and secure */

/* Max firmware data fetch size in bytes */
#define GB_FIRMWARE_FETCH_MAX			2000

/* Firmware protocol firmware size request/response */
struct gb_firmware_size_request {
	__u8			stage;
};

struct gb_firmware_size_response {
	__le32			size;
};

/* Firmware protocol get firmware request/response */
struct gb_firmware_get_firmware_request {
	__le32			offset;
	__le32			size;
};

struct gb_firmware_get_firmware_response {
	__u8			data[0];
};

/* Firmware protocol Ready to boot request */
struct gb_firmware_ready_to_boot_request {
	__u8			stage;
	__u8			status;
};
/* Firmware protocol Ready to boot response has no payload */


/* BATTERY */

/* Version of the Greybus battery protocol we support */
#define	GB_BATTERY_VERSION_MAJOR		0x00
#define	GB_BATTERY_VERSION_MINOR		0x01

/* Greybus battery request types */
#define	GB_BATTERY_TYPE_TECHNOLOGY		0x02
#define	GB_BATTERY_TYPE_STATUS			0x03
#define	GB_BATTERY_TYPE_MAX_VOLTAGE		0x04
#define	GB_BATTERY_TYPE_PERCENT_CAPACITY	0x05
#define	GB_BATTERY_TYPE_TEMPERATURE		0x06
#define	GB_BATTERY_TYPE_VOLTAGE			0x07
#define	GB_BATTERY_TYPE_CURRENT			0x08
#define GB_BATTERY_TYPE_CAPACITY		0x09	// TODO - POWER_SUPPLY_PROP_CURRENT_MAX
#define GB_BATTERY_TYPE_SHUTDOWN_TEMP		0x0a	// TODO - POWER_SUPPLY_PROP_TEMP_ALERT_MAX

/* Should match up with battery types in linux/power_supply.h */
#define GB_BATTERY_TECH_UNKNOWN			0x0000
#define GB_BATTERY_TECH_NiMH			0x0001
#define GB_BATTERY_TECH_LION			0x0002
#define GB_BATTERY_TECH_LIPO			0x0003
#define GB_BATTERY_TECH_LiFe			0x0004
#define GB_BATTERY_TECH_NiCd			0x0005
#define GB_BATTERY_TECH_LiMn			0x0006

struct gb_battery_technology_response {
	__le32	technology;
};

/* Should match up with battery status in linux/power_supply.h */
#define GB_BATTERY_STATUS_UNKNOWN		0x0000
#define GB_BATTERY_STATUS_CHARGING		0x0001
#define GB_BATTERY_STATUS_DISCHARGING		0x0002
#define GB_BATTERY_STATUS_NOT_CHARGING		0x0003
#define GB_BATTERY_STATUS_FULL			0x0004

struct gb_battery_status_response {
	__le16	battery_status;
};

struct gb_battery_max_voltage_response {
	__le32	max_voltage;
};

struct gb_battery_capacity_response {
	__le32	capacity;
};

struct gb_battery_temperature_response {
	__le32	temperature;
};

struct gb_battery_voltage_response {
	__le32	voltage;
};


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
};

/* HID set report request */
struct gb_hid_set_report_request {
	__u8				report_type;
	__u8				report_id;
	__u8				report[0];
};

/* HID input report request, via interrupt pipe */
struct gb_hid_input_report_request {
	__u8				report[0];
};


/* I2C */

/* Version of the Greybus i2c protocol we support */
#define GB_I2C_VERSION_MAJOR		0x00
#define GB_I2C_VERSION_MINOR		0x01

/* Greybus i2c request types */
#define GB_I2C_TYPE_FUNCTIONALITY	0x02
#define GB_I2C_TYPE_TIMEOUT		0x03
#define GB_I2C_TYPE_RETRIES		0x04
#define GB_I2C_TYPE_TRANSFER		0x05

#define GB_I2C_RETRIES_DEFAULT		3
#define GB_I2C_TIMEOUT_DEFAULT		1000	/* milliseconds */

/* functionality request has no payload */
struct gb_i2c_functionality_response {
	__le32	functionality;
};

struct gb_i2c_timeout_request {
	__le16	msec;
};
/* timeout response has no payload */

struct gb_i2c_retries_request {
	__u8	retries;
};
/* retries response has no payload */

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
};

struct gb_i2c_transfer_request {
	__le16				op_count;
	struct gb_i2c_transfer_op	ops[0];		/* op_count of these */
};
struct gb_i2c_transfer_response {
	__u8				data[0];	/* inbound data */
};


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
};

struct gb_gpio_activate_request {
	__u8	which;
};
/* activate response has no payload */

struct gb_gpio_deactivate_request {
	__u8	which;
};
/* deactivate response has no payload */

struct gb_gpio_get_direction_request {
	__u8	which;
};
struct gb_gpio_get_direction_response {
	__u8	direction;
};

struct gb_gpio_direction_in_request {
	__u8	which;
};
/* direction in response has no payload */

struct gb_gpio_direction_out_request {
	__u8	which;
	__u8	value;
};
/* direction out response has no payload */

struct gb_gpio_get_value_request {
	__u8	which;
};
struct gb_gpio_get_value_response {
	__u8	value;
};

struct gb_gpio_set_value_request {
	__u8	which;
	__u8	value;
};
/* set value response has no payload */

struct gb_gpio_set_debounce_request {
	__u8	which;
	__le16	usec;
} __packed;
/* debounce response has no payload */

struct gb_gpio_irq_type_request {
	__u8	which;
	__u8	type;
};
/* irq type response has no payload */

struct gb_gpio_irq_mask_request {
	__u8	which;
};
/* irq mask response has no payload */

struct gb_gpio_irq_unmask_request {
	__u8	which;
};
/* irq unmask response has no payload */

/* irq event requests originate on another module and are handled on the AP */
struct gb_gpio_irq_event_request {
	__u8	which;
};
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
};

struct gb_pwm_activate_request {
	__u8	which;
};

struct gb_pwm_deactivate_request {
	__u8	which;
};

struct gb_pwm_config_request {
	__u8	which;
	__le32	duty;
	__le32	period;
} __packed;

struct gb_pwm_polarity_request {
	__u8	which;
	__u8	polarity;
};

struct gb_pwm_enable_request {
	__u8	which;
};

struct gb_pwm_disable_request {
	__u8	which;
};

/* I2S */

#define GB_I2S_MGMT_TYPE_GET_SUPPORTED_CONFIGURATIONS	0x02
#define GB_I2S_MGMT_TYPE_SET_CONFIGURATION		0x03
#define GB_I2S_MGMT_TYPE_SET_SAMPLES_PER_MESSAGE	0x04
#define GB_I2S_MGMT_TYPE_GET_PROCESSING_DELAY		0x05
#define GB_I2S_MGMT_TYPE_SET_START_DELAY		0x06
#define GB_I2S_MGMT_TYPE_ACTIVATE_CPORT			0x07
#define GB_I2S_MGMT_TYPE_DEACTIVATE_CPORT		0x08
#define GB_I2S_MGMT_TYPE_REPORT_EVENT			0x09

#define GB_I2S_MGMT_BYTE_ORDER_NA			BIT(0)
#define GB_I2S_MGMT_BYTE_ORDER_BE			BIT(1)
#define GB_I2S_MGMT_BYTE_ORDER_LE			BIT(2)

#define GB_I2S_MGMT_SPATIAL_LOCATION_FL			BIT(0)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FR			BIT(1)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FC			BIT(2)
#define GB_I2S_MGMT_SPATIAL_LOCATION_LFE		BIT(3)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BL			BIT(4)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BR			BIT(5)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FLC		BIT(6)
#define GB_I2S_MGMT_SPATIAL_LOCATION_FRC		BIT(7)
#define GB_I2S_MGMT_SPATIAL_LOCATION_C			BIT(8) /* BC in USB */
#define GB_I2S_MGMT_SPATIAL_LOCATION_SL			BIT(9)
#define GB_I2S_MGMT_SPATIAL_LOCATION_SR			BIT(10)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TC			BIT(11)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFL		BIT(12)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFC		BIT(13)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFR		BIT(14)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TBL		BIT(15)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TBC		BIT(16)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TBR		BIT(17)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFLC		BIT(18)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TFRC		BIT(19)
#define GB_I2S_MGMT_SPATIAL_LOCATION_LLFE		BIT(20)
#define GB_I2S_MGMT_SPATIAL_LOCATION_RLFE		BIT(21)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TSL		BIT(22)
#define GB_I2S_MGMT_SPATIAL_LOCATION_TSR		BIT(23)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BC			BIT(24)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BLC		BIT(25)
#define GB_I2S_MGMT_SPATIAL_LOCATION_BRC		BIT(26)
#define GB_I2S_MGMT_SPATIAL_LOCATION_RD			BIT(31)

#define GB_I2S_MGMT_PROTOCOL_PCM			BIT(0)
#define GB_I2S_MGMT_PROTOCOL_I2S			BIT(1)
#define GB_I2S_MGMT_PROTOCOL_LR_STEREO			BIT(2)

#define GB_I2S_MGMT_ROLE_MASTER				BIT(0)
#define GB_I2S_MGMT_ROLE_SLAVE				BIT(1)

#define GB_I2S_MGMT_POLARITY_NORMAL			BIT(0)
#define GB_I2S_MGMT_POLARITY_REVERSED			BIT(1)

#define GB_I2S_MGMT_EDGE_RISING				BIT(0)
#define GB_I2S_MGMT_EDGE_FALLING			BIT(1)

#define GB_I2S_MGMT_EVENT_UNSPECIFIED			0x1
#define GB_I2S_MGMT_EVENT_HALT				0x2
#define GB_I2S_MGMT_EVENT_INTERNAL_ERROR		0x3
#define GB_I2S_MGMT_EVENT_PROTOCOL_ERROR		0x4
#define GB_I2S_MGMT_EVENT_FAILURE			0x5
#define GB_I2S_MGMT_EVENT_OUT_OF_SEQUENCE		0x6
#define GB_I2S_MGMT_EVENT_UNDERRUN			0x7
#define GB_I2S_MGMT_EVENT_OVERRUN			0x8
#define GB_I2S_MGMT_EVENT_CLOCKING			0x9
#define GB_I2S_MGMT_EVENT_DATA_LEN			0xa

struct gb_i2s_mgmt_configuration {
	__le32	sample_frequency;
	__u8	num_channels;
	__u8	bytes_per_channel;
	__u8	byte_order;
	__u8	pad;
	__le32	spatial_locations;
	__le32	ll_protocol;
	__u8	ll_mclk_role;
	__u8	ll_bclk_role;
	__u8	ll_wclk_role;
	__u8	ll_wclk_polarity;
	__u8	ll_wclk_change_edge;
	__u8	ll_wclk_tx_edge;
	__u8	ll_wclk_rx_edge;
	__u8	ll_data_offset;
};

/* get supported configurations request has no payload */
struct gb_i2s_mgmt_get_supported_configurations_response {
	__u8					config_count;
	__u8					pad[3];
	struct gb_i2s_mgmt_configuration	config[0];
};

struct gb_i2s_mgmt_set_configuration_request {
	struct gb_i2s_mgmt_configuration	config;
};
/* set configuration response has no payload */

struct gb_i2s_mgmt_set_samples_per_message_request {
	__le16	samples_per_message;
};
/* set samples per message response has no payload */

/* get processing request delay has no payload */
struct gb_i2s_mgmt_get_processing_delay_response {
	__le32	microseconds;
};

struct gb_i2s_mgmt_set_start_delay_request {
	__le32	microseconds;
};
/* set start delay response has no payload */

struct gb_i2s_mgmt_activate_cport_request {
	__le16	cport;
};
/* activate cport response has no payload */

struct gb_i2s_mgmt_deactivate_cport_request {
	__le16	cport;
};
/* deactivate cport response has no payload */

struct gb_i2s_mgmt_report_event_request {
	__u8	event;
};
/* report event response has no payload */

#define GB_I2S_DATA_TYPE_SEND_DATA			0x02

struct gb_i2s_send_data_request {
	__le32	sample_number;
	__le32	size;
	__u8	data[0];
};
/* send data has no response at all */


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
#define GB_SPI_TYPE_MODE		0x02
#define GB_SPI_TYPE_FLAGS		0x03
#define GB_SPI_TYPE_BITS_PER_WORD_MASK	0x04
#define GB_SPI_TYPE_NUM_CHIPSELECT	0x05
#define GB_SPI_TYPE_TRANSFER		0x06

/* mode request has no payload */
struct gb_spi_mode_response {
	__le16	mode;
};

/* flags request has no payload */
struct gb_spi_flags_response {
	__le16	flags;
};

/* bits-per-word request has no payload */
struct gb_spi_bpw_response {
	__le32	bits_per_word_mask;
};

/* num-chipselects request has no payload */
struct gb_spi_chipselect_response {
	__le16	num_chipselect;
};

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
};

struct gb_spi_transfer_request {
	__u8			chip_select;	/* of the spi device */
	__u8			mode;		/* of the spi device */
	__le16			count;
	struct gb_spi_transfer	transfers[0];	/* count of these */
};

struct gb_spi_transfer_response {
	__u8			data[0];	/* inbound data */
};

/* Version of the Greybus SVC protocol we support */
#define GB_SVC_VERSION_MAJOR		0x00
#define GB_SVC_VERSION_MINOR		0x01

/* Greybus SVC request types */
#define GB_SVC_TYPE_SVC_HELLO		0x02
#define GB_SVC_TYPE_INTF_DEVICE_ID	0x03
#define GB_SVC_TYPE_INTF_HOTPLUG	0x04
#define GB_SVC_TYPE_INTF_HOT_UNPLUG	0x05
#define GB_SVC_TYPE_INTF_RESET		0x06
#define GB_SVC_TYPE_CONN_CREATE		0x07
#define GB_SVC_TYPE_CONN_DESTROY	0x08
#define GB_SVC_TYPE_ROUTE_CREATE	0x0b

/* SVC version request/response have same payload as gb_protocol_version_response */

/* SVC protocol hello request */
struct gb_svc_hello_request {
	__le16			endo_id;
	__u8			interface_id;
} __packed;
/* hello response has no payload */

struct gb_svc_intf_device_id_request {
	__u8	intf_id;
	__u8	device_id;
};
/* device id response has no payload */

struct gb_svc_intf_hotplug_request {
	__u8	intf_id;
	struct {
		__le32	unipro_mfg_id;
		__le32	unipro_prod_id;
		__le32	ara_vend_id;
		__le32	ara_prod_id;
	} data;
} __packed;
/* hotplug response has no payload */

struct gb_svc_intf_hot_unplug_request {
	__u8	intf_id;
};
/* hot unplug response has no payload */

struct gb_svc_intf_reset_request {
	__u8	intf_id;
};
/* interface reset response has no payload */

struct gb_svc_conn_create_request {
	__u8	intf1_id;
	__u16	cport1_id;
	__u8	intf2_id;
	__u16	cport2_id;
	__u8	tc;
	__u8	flags;
} __packed;
/* connection create response has no payload */

struct gb_svc_conn_destroy_request {
	__u8	intf1_id;
	__u16	cport1_id;
	__u8	intf2_id;
	__u16	cport2_id;
} __packed;
/* connection destroy response has no payload */

struct gb_svc_route_create_request {
	__u8	intf1_id;
	__u8	dev1_id;
	__u8	intf2_id;
	__u8	dev2_id;
};


/* RAW */

/* Version of the Greybus raw protocol we support */
#define	GB_RAW_VERSION_MAJOR			0x00
#define	GB_RAW_VERSION_MINOR			0x01

/* Greybus raw request types */
#define	GB_RAW_TYPE_SEND			0x02

struct gb_raw_send_request {
	__le32	len;
	__u8	data[0];
};


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
};

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
};

struct gb_uart_set_break_request {
	__u8	state;
};

/* input control lines and line errors */
#define GB_UART_CTRL_DCD			0x01
#define GB_UART_CTRL_DSR			0x02
#define GB_UART_CTRL_RI				0x04

struct gb_uart_serial_state_request {
	__u8	control;
};

/* Loopback */

/* Version of the Greybus loopback protocol we support */
#define GB_LOOPBACK_VERSION_MAJOR		0x00
#define GB_LOOPBACK_VERSION_MINOR		0x01

/* Greybus loopback request types */
#define GB_LOOPBACK_TYPE_PING			0x02
#define GB_LOOPBACK_TYPE_TRANSFER		0x03
#define GB_LOOPBACK_TYPE_SINK			0x04

struct gb_loopback_transfer_request {
	__le32	len;
	__u8	data[0];
};

struct gb_loopback_transfer_response {
	__u8	data[0];
};

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
};

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
#define GB_SDIO_CMD_BCR		0x02
#define GB_SDIO_CMD_BC		0x03

	__le32	cmd_arg;
} __packed;

struct gb_sdio_command_response {
	__le32	resp[4];
};

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
};

/* event request: generated by module and is defined as unidirectional */
struct gb_sdio_event_request {
	__u8	event;
#define GB_SDIO_CARD_INSERTED	0x01
#define GB_SDIO_CARD_REMOVED	0x02
#define GB_SDIO_WP		0x04
};

/* Lights */

#define GB_LIGHTS_VERSION_MAJOR 0x00
#define GB_LIGHTS_VERSION_MINOR 0x01

/* Greybus Lights request types */
#define GB_LIGHTS_TYPE_INVALID			0x00
#define GB_LIGHTS_TYPE_PROTOCOL_VERSION		0x01
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
};

/* light config request payload */
struct gb_lights_get_light_config_request {
	__u8	id;
};

/* light config response payload */
struct gb_lights_get_light_config_response {
	__u8	channel_count;
	__u8	name[32];
};

/* channel config request payload */
struct gb_lights_get_channel_config_request {
	__u8	light_id;
	__u8	channel_id;
};

/* channel flash config request payload */
struct gb_lights_get_channel_flash_config_request {
	__u8	light_id;
	__u8	channel_id;
};

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
};

/* blink request payload: response have no payload */
struct gb_lights_blink_request {
	__u8	light_id;
	__u8	channel_id;
	__le16	time_on_ms;
	__le16	time_off_ms;
};

/* set brightness request payload: response have no payload */
struct gb_lights_set_brightness_request {
	__u8	light_id;
	__u8	channel_id;
	__u8	brightness;
};

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
};

/* event request: generated by module */
struct gb_lights_event_request {
	__u8	light_id;
	__u8	event;
#define GB_LIGHTS_LIGHT_CONFIG		0x01
};

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
};

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
};

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
};

#endif /* __GREYBUS_PROTOCOLS_H */

